//! @file serverModule.cpp
//! @brief ServerModule implementation: socket setup, the worker threads and the
//!        TCP/UDP queues.
//!
//! The API is documented in serverModule.h. All logging here uses the SPDLOG_
//! macros, which are compiled out unless the build sets VERBOSE_LOGGING=ON.

#include "serverModule.h"
#include <unistd.h>
#include <cstring>
#include <cerrno>
#include <climits>
#include <ifaddrs.h>
#include <net/if.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <spdlog/spdlog.h>
#include <algorithm>

namespace {
//! @brief Predicate for the client list: the connection is still up.
//! @param c Client handle to test.
//! @return Its ClientHandle::alive flag.
bool isAlive(const std::shared_ptr<ClientHandle>& c) { return c->alive.load(); }

//! @brief socket() + SO_REUSEADDR + bind() to INADDR_ANY:port.
//! @param type  SOCK_STREAM (TCP) or SOCK_DGRAM (UDP).
//! @param port  Port to bind.
//! @param proto Prefix of the error messages ("TCP" / "UDP"). Only the SPDLOG_
//!              macros use it, and they can be compiled out (hence [[maybe_unused]]).
//! @return The bound socket, or -1 on failure (logged with proto as the prefix; a
//!         socket already created is closed first).
int openBoundSocket(int type, int port, [[maybe_unused]] const char* proto) {
    int fd = socket(AF_INET, type, 0);
    if (fd < 0) {
        SPDLOG_ERROR("{} socket() failed: {}", proto, strerror(errno));
        return -1;
    }

    //! SO_REUSEADDR: a restarted server can bind the port again while TCP
    //! connections from its previous run are still in TIME_WAIT (result unchecked)
    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        SPDLOG_ERROR("{} bind on port {} failed: {}", proto, port, strerror(errno));
        close(fd);
        return -1;
    }
    return fd;
}
} // namespace

// ============================================================================
//  Construction / destruction
// ============================================================================

ServerModule::ServerModule(int tcpPort, int udpPort, const std::string& netDevice)
    : tcpPort_(tcpPort), udpPort_(udpPort)
{
    resolveNetwork(netDevice);
    SPDLOG_INFO("ServerModule constructed with TCP port {}, UDP port {}, net device '{}'",
                tcpPort_, udpPort_, netDevice.empty() ? "auto-detect" : netDevice);
}

ServerModule::~ServerModule() {
    stop();
}

// ============================================================================
//  Network detection
// ============================================================================

void ServerModule::resolveNetwork(const std::string& requestedDevice) {
    if (!requestedDevice.empty()) {
        netDevice_ = requestedDevice;
        ip_ = "0.0.0.0";  // fallback if lookup fails

        struct ifaddrs* ifap = nullptr;
        if (getifaddrs(&ifap) == 0) {
            for (struct ifaddrs* ifa = ifap; ifa; ifa = ifa->ifa_next) {
                if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) continue;
                if (std::string(ifa->ifa_name) != requestedDevice) continue;
                char ipbuf[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &reinterpret_cast<struct sockaddr_in*>(ifa->ifa_addr)->sin_addr,
                          ipbuf, sizeof(ipbuf));
                ip_ = ipbuf;
                break;
            }
            freeifaddrs(ifap);
        } else {
            SPDLOG_WARN("getifaddrs failed: {}", strerror(errno));
        }

        SPDLOG_INFO("ServerModule: interface={} ip={}", netDevice_, ip_);
        return;
    }

    struct ifaddrs* ifap = nullptr;
    if (getifaddrs(&ifap) != 0) {
        SPDLOG_WARN("getifaddrs failed: {}", strerror(errno));
        ip_ = "0.0.0.0";
        netDevice_ = "any";
        return;
    }

    struct ifaddrs* best = nullptr;
    int bestPriority = INT_MAX;

    for (struct ifaddrs* ifa = ifap; ifa; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) continue;
        if (!(ifa->ifa_flags & IFF_UP) || (ifa->ifa_flags & IFF_LOOPBACK)) continue;

        std::string name(ifa->ifa_name);

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
        SPDLOG_WARN("No suitable network interface found, reporting 0.0.0.0");
        ip_ = "0.0.0.0";
        netDevice_ = "any";
    }

    freeifaddrs(ifap);
    SPDLOG_INFO("ServerModule: interface={} ip={}", netDevice_, ip_);
}

// ============================================================================
//  Socket initialisation
// ============================================================================

bool ServerModule::initTCP() {
    tcpListenFd_ = openBoundSocket(SOCK_STREAM, tcpPort_, "TCP");
    if (tcpListenFd_ < 0) return false;

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
    udpFd_ = openBoundSocket(SOCK_DGRAM, udpPort_, "UDP");
    if (udpFd_ < 0) return false;

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

    // Shut down + close the listening socket: shutdown() makes a blocked
    // accept() return with an error (close() alone would not wake it).
    if (tcpListenFd_ >= 0) {
        shutdown(tcpListenFd_, SHUT_RDWR);
        close(tcpListenFd_);
        tcpListenFd_ = -1;
    }

    // Join the accept thread first so no new ClientHandles are added after this point.
    if (tcpAcceptThread_.joinable()) tcpAcceptThread_.join();

    // Collect client handles, shut down and close their fds (shutdown() unblocks
    // recv()), then join their threads.
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

    // Shut down + close the UDP socket. close() alone does not wake a thread
    // blocked in recvfrom() on Linux; shutdown() makes it return 0.
    if (udpFd_ >= 0) {
        shutdown(udpFd_, SHUT_RDWR);
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
        client->id   = nextClientId_++;
        client->fd   = fd;
        client->ip   = inet_ntoa(clientAddr.sin_addr);
        client->port = ntohs(clientAddr.sin_port);
        SPDLOG_INFO("TCP client connected: {}:{}", client->ip, client->port);

        // Prune dead handles before adding the new one. They are moved out
        // under the lock and joined after it, so a slow RX thread never
        // blocks the TUI (which reads the client list every frame).
        std::vector<std::shared_ptr<ClientHandle>> dead;
        {
            std::lock_guard<std::mutex> lock(clientsMutex_);
            auto mid = std::stable_partition(clients_.begin(), clients_.end(), isAlive);
            dead.assign(std::make_move_iterator(mid), std::make_move_iterator(clients_.end()));
            clients_.erase(mid, clients_.end());
        }
        for (auto& c : dead) {
            if (c->fd >= 0) shutdown(c->fd, SHUT_RDWR);
            if (c->rxThread.joinable()) c->rxThread.join();
            if (c->fd >= 0) { close(c->fd); }
        }

        std::lock_guard<std::mutex> lock(clientsMutex_);

        // Enforce the client limit (TCP only). The lock_guard is scoped to this
        // iteration, so `continue` releases it.
        size_t alive = std::count_if(clients_.begin(), clients_.end(), isAlive);
        if (limitEnabled_.load() && alive >= maxClients_.load()) {
            //! Tell the client why before closing it (MSG_NOSIGNAL: no SIGPIPE
            //! if it has already gone)
            static const char msg[] = "E: server full\n";
            send(fd, msg, sizeof(msg) - 1, MSG_NOSIGNAL);
            shutdown(fd, SHUT_RDWR);
            close(fd);
            SPDLOG_WARN("Rejected TCP client {}:{} (max {} reached)",
                        client->ip, client->port, maxClients_.load());
            continue;
        }

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
            tcpRxQueue_.emplace(client->id, std::string(buf, n));
        }
        SPDLOG_DEBUG("TCP RX from {}:{}: {} bytes", client->ip, client->port, n);
    }

    // Signal dead — fd will be closed by the accept thread's prune pass or stop().
    client->alive = false;
}

// ============================================================================
//  TCP — shared TX loop (to one client, or broadcast to all alive clients)
// ============================================================================

void ServerModule::tcpTxLoop() {
    while (running_.load()) {
        std::unique_lock<std::mutex> lock(tcpTxMutex_);
        tcpTxCV_.wait(lock, [this] {
            return !tcpTxQueue_.empty() || !running_.load();
        });

        while (!tcpTxQueue_.empty()) {
            //! Take one packet and send it without the queue lock, so
            //! sendCommand() callers never wait on a socket
            auto [to, msg] = std::move(tcpTxQueue_.front());
            tcpTxQueue_.pop();
            lock.unlock();

            {
                std::lock_guard<std::mutex> clientLock(clientsMutex_);
                for (auto& c : clients_) {
                    if (!c->alive.load() || c->fd < 0) continue;
                    if (to != 0 && c->id != to) continue;
                    ssize_t sent = send(c->fd, msg.c_str(), msg.size(), MSG_NOSIGNAL);
                    if (sent < 0) {
                        SPDLOG_ERROR("TCP send to {}:{} failed: {}", c->ip, c->port, strerror(errno));
                        c->alive = false;
                        //! wake the RX thread out of recv() so the prune can join it
                        shutdown(c->fd, SHUT_RDWR);
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
    return sendCommandTo(0, message);
}

bool ServerModule::sendCommandTo(uint64_t clientId, const std::string& message) {
    if (!running_.load()) return false;
    {
        std::lock_guard<std::mutex> lock(tcpTxMutex_);
        tcpTxQueue_.emplace(clientId, message);
    }
    tcpTxCV_.notify_one();
    return true;
}

bool ServerModule::receiveCommand(std::string& message, uint64_t* from) {
    std::lock_guard<std::mutex> lock(tcpRxMutex_);
    if (tcpRxQueue_.empty()) return false;
    if (from) *from = tcpRxQueue_.front().first;
    message = std::move(tcpRxQueue_.front().second);
    tcpRxQueue_.pop();
    return true;
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

        // Register the sender so the TX loop sends it the continuous stream
        // (emplace() keeps the first-seen time of a peer already registered).
        {
            std::lock_guard<std::mutex> lock(udpClientsMutex_);
            udpClients_.emplace(std::make_pair(senderIP, senderPort),
                                std::chrono::steady_clock::now());
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
        std::vector<std::pair<std::string, int>> targets;
        {
            std::lock_guard<std::mutex> lock(udpClientsMutex_);
            for (auto& entry : udpClients_) targets.push_back(entry.first);
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

// ============================================================================
//  Client inspection (used by the TUI)
// ============================================================================

std::vector<ClientInfo> ServerModule::getClients() const {
    std::vector<ClientInfo> out;
    {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        for (auto& c : clients_) {
            if (!c->alive.load()) continue;
            out.push_back({"TCP", c->ip, c->port, c->connectedAt});
        }
    }
    {
        std::lock_guard<std::mutex> lock(udpClientsMutex_);
        for (auto& [peer, since] : udpClients_) {
            out.push_back({"UDP", peer.first, peer.second, since});
        }
    }
    return out;
}

size_t ServerModule::tcpClientCount() const {
    std::lock_guard<std::mutex> lock(clientsMutex_);
    return std::count_if(clients_.begin(), clients_.end(), isAlive);
}

size_t ServerModule::udpClientCount() const {
    std::lock_guard<std::mutex> lock(udpClientsMutex_);
    return udpClients_.size();
}

void ServerModule::clearUdpClients() {
    std::lock_guard<std::mutex> lock(udpClientsMutex_);
    udpClients_.clear();
}
