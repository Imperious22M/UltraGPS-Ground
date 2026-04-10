#include "serverModule.h"
#include <unistd.h>
#include <cstring>
#include <cerrno>
#include <climits>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>

// ============================================================================
//  Construction / destruction
// ============================================================================

ServerModule::ServerModule(int tcpPort, int udpPort, const std::string& netDevice)
    : tcpPort_(tcpPort), udpPort_(udpPort)
{
    resolveNetwork(netDevice);
}

ServerModule::~ServerModule() {
    stop();
}

// ============================================================================
//  Network detection
// ============================================================================

void ServerModule::resolveNetwork(const std::string& requestedDevice) {
    struct ifaddrs* ifap = nullptr;
    if (getifaddrs(&ifap) != 0) {
        SPDLOG_WARN("getifaddrs failed: {}", strerror(errno));
        ip_ = "0.0.0.0";
        netDevice_ = requestedDevice.empty() ? "any" : requestedDevice;
        return;
    }

    struct ifaddrs* best = nullptr;
    int bestPriority = INT_MAX;

    for (struct ifaddrs* ifa = ifap; ifa; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) continue;
        if (!(ifa->ifa_flags & IFF_UP) || (ifa->ifa_flags & IFF_LOOPBACK)) continue;

        std::string name(ifa->ifa_name);

        if (!requestedDevice.empty()) {
            if (name == requestedDevice) { best = ifa; break; }
            continue;
        }

        // Prefer wireless, then wired ethernet, then anything else.
        int priority;
        if (name.rfind("wl", 0) == 0)                                   priority = 0;
        else if (name.rfind("eth", 0) == 0 || name.rfind("en", 0) == 0) priority = 1;
        else                                                              priority = 2;

        if (priority < bestPriority) { bestPriority = priority; best = ifa; }
    }

    if (best) {
        char ipbuf[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &reinterpret_cast<struct sockaddr_in*>(best->ifa_addr)->sin_addr,
                  ipbuf, sizeof(ipbuf));
        ip_ = ipbuf;
        netDevice_ = best->ifa_name;
    } else {
        SPDLOG_WARN("No suitable network interface found, binding to INADDR_ANY");
        ip_ = "0.0.0.0";
        netDevice_ = requestedDevice.empty() ? "any" : requestedDevice;
    }

    freeifaddrs(ifap);
    SPDLOG_INFO("ServerModule: interface={} ip={}", netDevice_, ip_);
}

// ============================================================================
//  Socket initialisation
// ============================================================================

bool ServerModule::initTCP() {
    tcpListenFd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (tcpListenFd_ < 0) {
        SPDLOG_ERROR("TCP socket() failed: {}", strerror(errno));
        return false;
    }

    int opt = 1;
    setsockopt(tcpListenFd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(tcpPort_);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(tcpListenFd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        SPDLOG_ERROR("TCP bind on port {} failed: {}", tcpPort_, strerror(errno));
        close(tcpListenFd_);
        tcpListenFd_ = -1;
        return false;
    }

    if (listen(tcpListenFd_, 8) < 0) {
        SPDLOG_ERROR("TCP listen() failed: {}", strerror(errno));
        close(tcpListenFd_);
        tcpListenFd_ = -1;
        return false;
    }

    SPDLOG_INFO("TCP listener on port {}", tcpPort_);
    return true;
}

bool ServerModule::initUDP() {
    udpFd_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (udpFd_ < 0) {
        SPDLOG_ERROR("UDP socket() failed: {}", strerror(errno));
        return false;
    }

    int opt = 1;
    setsockopt(udpFd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(udpPort_);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(udpFd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        SPDLOG_ERROR("UDP bind on port {} failed: {}", udpPort_, strerror(errno));
        close(udpFd_);
        udpFd_ = -1;
        return false;
    }

    SPDLOG_INFO("UDP socket on port {}", udpPort_);
    return true;
}

// ============================================================================
//  Start / stop
// ============================================================================

bool ServerModule::start() {
    if (running_.load()) {
        SPDLOG_WARN("ServerModule already running");
        return false;
    }

    if (!initTCP() || !initUDP()) return false;

    running_ = true;

    tcpAcceptThread_ = std::thread(&ServerModule::tcpAcceptLoop, this);
    tcpTxThread_     = std::thread(&ServerModule::tcpTxLoop,     this);
    udpRxThread_     = std::thread(&ServerModule::udpRxLoop,     this);
    udpTxThread_     = std::thread(&ServerModule::udpTxLoop,     this);

    SPDLOG_INFO("ServerModule started (TCP:{} UDP:{})", tcpPort_, udpPort_);
    return true;
}

void ServerModule::stop() {
    if (!running_.exchange(false)) return;

    // Unblock condition-variable waiters.
    tcpTxCV_.notify_all();
    udpTxCV_.notify_all();

    // Close the listening socket — causes accept() to return with an error.
    if (tcpListenFd_ >= 0) {
        shutdown(tcpListenFd_, SHUT_RDWR);
        close(tcpListenFd_);
        tcpListenFd_ = -1;
    }

    // Join the accept thread first so no new ClientHandles are added after this point.
    if (tcpAcceptThread_.joinable()) tcpAcceptThread_.join();

    // Collect client handles, close their fds (unblocks recv()), then join their threads.
    std::vector<std::shared_ptr<ClientHandle>> snapshot;
    {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        snapshot = clients_;
    }
    for (auto& c : snapshot) {
        c->alive = false;
        if (c->fd >= 0) {
            shutdown(c->fd, SHUT_RDWR);
            close(c->fd);
            c->fd = -1;
        }
    }
    for (auto& c : snapshot) {
        if (c->rxThread.joinable()) c->rxThread.join();
    }
    {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        clients_.clear();
    }

    // Close UDP socket — causes recvfrom() to return with an error.
    if (udpFd_ >= 0) {
        close(udpFd_);
        udpFd_ = -1;
    }

    if (tcpTxThread_.joinable())  tcpTxThread_.join();
    if (udpRxThread_.joinable())  udpRxThread_.join();
    if (udpTxThread_.joinable())  udpTxThread_.join();

    SPDLOG_INFO("ServerModule stopped");
}

// ============================================================================
//  TCP — accept loop
// ============================================================================

void ServerModule::tcpAcceptLoop() {
    while (running_.load()) {
        struct sockaddr_in clientAddr{};
        socklen_t len = sizeof(clientAddr);

        int fd = accept(tcpListenFd_,
                        reinterpret_cast<struct sockaddr*>(&clientAddr), &len);
        if (fd < 0) {
            if (running_.load()) {
                SPDLOG_ERROR("TCP accept() failed: {}", strerror(errno));
            }
            break;
        }

        auto client = std::make_shared<ClientHandle>();
        client->fd   = fd;
        client->ip   = inet_ntoa(clientAddr.sin_addr);
        client->port = ntohs(clientAddr.sin_port);
        SPDLOG_INFO("TCP client connected: {}:{}", client->ip, client->port);

        std::lock_guard<std::mutex> lock(clientsMutex_);

        // Prune dead handles before adding the new one.
        clients_.erase(
            std::remove_if(clients_.begin(), clients_.end(),
                [](const std::shared_ptr<ClientHandle>& c) {
                    if (!c->alive.load()) {
                        if (c->rxThread.joinable()) c->rxThread.join();
                        if (c->fd >= 0) { close(c->fd); }
                        return true;
                    }
                    return false;
                }),
            clients_.end());

        client->rxThread = std::thread(&ServerModule::tcpClientRxLoop, this, client);
        clients_.push_back(std::move(client));
    }
}

// ============================================================================
//  TCP — per-client RX loop
// ============================================================================

void ServerModule::tcpClientRxLoop(std::shared_ptr<ClientHandle> client) {
    char buf[4096];

    while (running_.load() && client->alive.load()) {
        ssize_t n = recv(client->fd, buf, sizeof(buf) - 1, 0);
        if (n <= 0) {
            if (n == 0) {
                SPDLOG_INFO("TCP client {}:{} disconnected", client->ip, client->port);
            } else if (running_.load()) {
                SPDLOG_ERROR("TCP recv error ({}:{}): {}", client->ip, client->port, strerror(errno));
            }
            break;
        }

        {
            std::lock_guard<std::mutex> lock(tcpRxMutex_);
            tcpRxQueue_.push(std::string(buf, n));
        }
        SPDLOG_DEBUG("TCP RX from {}:{}: {} bytes", client->ip, client->port, n);
    }

    // Signal dead — fd will be closed by the accept thread's prune pass or stop().
    client->alive = false;
}

// ============================================================================
//  TCP — shared TX loop (broadcasts to all alive clients)
// ============================================================================

void ServerModule::tcpTxLoop() {
    while (running_.load()) {
        std::unique_lock<std::mutex> lock(tcpTxMutex_);
        tcpTxCV_.wait(lock, [this] {
            return !tcpTxQueue_.empty() || !running_.load();
        });

        while (!tcpTxQueue_.empty()) {
            std::string msg = tcpTxQueue_.front();
            tcpTxQueue_.pop();
            lock.unlock();

            {
                std::lock_guard<std::mutex> clientLock(clientsMutex_);
                for (auto& c : clients_) {
                    if (!c->alive.load() || c->fd < 0) continue;
                    ssize_t sent = send(c->fd, msg.c_str(), msg.size(), MSG_NOSIGNAL);
                    if (sent < 0) {
                        SPDLOG_ERROR("TCP send to {}:{} failed: {}", c->ip, c->port, strerror(errno));
                        c->alive = false;
                    }
                }
            }

            lock.lock();
        }
    }
}

// ============================================================================
//  TCP — public interface
// ============================================================================

bool ServerModule::sendCommand(const std::string& message) {
    if (!running_.load()) return false;
    {
        std::lock_guard<std::mutex> lock(tcpTxMutex_);
        tcpTxQueue_.push(message);
    }
    tcpTxCV_.notify_one();
    return true;
}

bool ServerModule::receiveCommand(std::string& message) {
    std::lock_guard<std::mutex> lock(tcpRxMutex_);
    if (tcpRxQueue_.empty()) return false;
    message = tcpRxQueue_.front();
    tcpRxQueue_.pop();
    return true;
}

bool ServerModule::hasPendingCommands() const {
    std::lock_guard<std::mutex> lock(tcpRxMutex_);
    return !tcpRxQueue_.empty();
}

// ============================================================================
//  UDP — RX loop
// ============================================================================

void ServerModule::udpRxLoop() {
    char buf[4096];

    while (running_.load()) {
        struct sockaddr_in sender{};
        socklen_t slen = sizeof(sender);

        ssize_t n = recvfrom(udpFd_, buf, sizeof(buf) - 1, 0,
                             reinterpret_cast<struct sockaddr*>(&sender), &slen);
        if (n <= 0) {
            if (running_.load()) {
                SPDLOG_ERROR("UDP recvfrom() failed: {}", strerror(errno));
            }
            break;
        }

        std::string senderIP  = inet_ntoa(sender.sin_addr);
        int         senderPort = ntohs(sender.sin_port);

        // Register sender so the TX loop knows where to send replies.
        {
            std::lock_guard<std::mutex> lock(udpClientsMutex_);
            udpClients_.emplace(senderIP, senderPort);
        }

        {
            std::lock_guard<std::mutex> lock(udpRxMutex_);
            udpRxQueue_.push(std::string(buf, n));
        }

        SPDLOG_DEBUG("UDP RX from {}:{}: {} bytes", senderIP, senderPort, n);
    }
}

// ============================================================================
//  UDP — TX loop (sends latest packet as soon as it is updated)
// ============================================================================

void ServerModule::udpTxLoop() {
    while (running_.load()) {
        std::string msg;
        {
            std::unique_lock<std::mutex> lock(udpTxMutex_);
            udpTxCV_.wait(lock, [this] {
                return udpTxHasData_ || !running_.load();
            });

            if (!running_.load()) break;

            msg           = udpTxLatest_;
            udpTxHasData_ = false;
        }

        // Snapshot the client set so we don't hold the mutex during I/O.
        std::set<std::pair<std::string, int>> targets;
        {
            std::lock_guard<std::mutex> lock(udpClientsMutex_);
            targets = udpClients_;
        }

        for (auto& [ip, port] : targets) {
            struct sockaddr_in dest{};
            dest.sin_family = AF_INET;
            dest.sin_port   = htons(port);
            inet_pton(AF_INET, ip.c_str(), &dest.sin_addr);

            ssize_t sent = sendto(udpFd_, msg.c_str(), msg.size(), 0,
                                  reinterpret_cast<struct sockaddr*>(&dest), sizeof(dest));
            if (sent < 0) {
                SPDLOG_ERROR("UDP sendto {}:{} failed: {}", ip, port, strerror(errno));
            } else {
                SPDLOG_DEBUG("UDP TX to {}:{}: {} bytes", ip, port, sent);
            }
        }
    }
}

// ============================================================================
//  UDP — public interface
// ============================================================================

bool ServerModule::sendContinuous(const std::string& message) {
    if (!running_.load()) return false;
    {
        std::lock_guard<std::mutex> lock(udpTxMutex_);
        udpTxLatest_  = message;
        udpTxHasData_ = true;
    }
    udpTxCV_.notify_one();
    return true;
}

bool ServerModule::receiveContinuous(std::string& message) {
    std::lock_guard<std::mutex> lock(udpRxMutex_);
    if (udpRxQueue_.empty()) return false;
    message = udpRxQueue_.front();
    udpRxQueue_.pop();
    return true;
}

bool ServerModule::hasContinuousData() const {
    std::lock_guard<std::mutex> lock(udpRxMutex_);
    return !udpRxQueue_.empty();
}
