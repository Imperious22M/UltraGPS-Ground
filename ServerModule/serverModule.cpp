#include "serverModule.h"
#include "spdlog/spdlog.h"
#include <chrono>
#include <thread>
#include <unistd.h>
#include <cstring>
#include <cerrno>

UDPServer::UDPServer() : running(false), clientPort(0), clientConnected(false), clientTimeoutMs(5000) {}

UDPServer::~UDPServer() {
    stop();
}

bool UDPServer::addSendPort(int port, int destinationPort) {
    if (ports.find(port) != ports.end()) {
        SPDLOG_WARN("Port {} already exists", port);
        return false;
    }

    PortInfo portInfo;
    portInfo.type = PortType::SEND;
    portInfo.destinationPort = destinationPort;

    if (!createSocket(port, portInfo)) {
        SPDLOG_ERROR("Failed to create socket for port {}", port);
        return false;
    }

    ports[port] = std::move(portInfo);
    if (destinationPort != 0) {
        SPDLOG_INFO("Added UDP send port {} -> client port {}", port, destinationPort);
    } else {
        SPDLOG_INFO("Added UDP send port {} (dynamic destination)", port);
    }

    if (running) {
        ports[port].mainThread = std::thread(&UDPServer::sendLoop, this, port);
    }

    return true;
}

bool UDPServer::addListenPort(int port) {
    if (ports.find(port) != ports.end()) {
        SPDLOG_WARN("Port {} already exists", port);
        return false;
    }

    PortInfo portInfo;
    portInfo.type = PortType::LISTEN;
    portInfo.destinationPort = 0;

    if (!createSocket(port, portInfo)) {
        SPDLOG_ERROR("Failed to create socket for port {}", port);
        return false;
    }

    ports[port] = std::move(portInfo);
    SPDLOG_INFO("Added UDP listen port {}", port);

    if (running) {
        ports[port].mainThread = std::thread(&UDPServer::receiveLoop, this, port);
    }

    return true;
}

bool UDPServer::removePort(int port) {
    auto it = ports.find(port);
    if (it == ports.end()) {
        SPDLOG_WARN("Port {} not found", port);
        return false;
    }

    if (it->second.mainThread.joinable()) {
        it->second.mainThread.join();
    }

    closeSocket(it->second);
    ports.erase(it);

    SPDLOG_INFO("Removed port {}", port);
    return true;
}

bool UDPServer::start() {
    if (running) {
        SPDLOG_WARN("Server is already running");
        return false;
    }

    if (ports.empty()) {
        SPDLOG_WARN("No ports configured");
        return false;
    }

    running = true;

    for (auto& [port, portInfo] : ports) {
        if (portInfo.type == PortType::SEND) {
            portInfo.mainThread = std::thread(&UDPServer::sendLoop, this, port);
        } else if (portInfo.type == PortType::LISTEN) {
            portInfo.mainThread = std::thread(&UDPServer::receiveLoop, this, port);
        }
    }

    SPDLOG_INFO("UDP server started with {} ports", ports.size());
    return true;
}

void UDPServer::stop() {
    if (!running) {
        return;
    }

    running = false;
    outgoingQueueCV.notify_all();

    for (auto& [port, portInfo] : ports) {
        if (portInfo.mainThread.joinable()) {
            portInfo.mainThread.join();
        }
        closeSocket(portInfo);
    }

    SPDLOG_INFO("UDP server stopped");
}

bool UDPServer::createSocket(int port, PortInfo& portInfo) {
    portInfo.socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (portInfo.socket_fd < 0) {
        SPDLOG_ERROR("Failed to create socket for port {}: {}", port, strerror(errno));
        return false;
    }

    int reuse = 1;
    if (setsockopt(portInfo.socket_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        SPDLOG_ERROR("Failed to enable SO_REUSEADDR for port {}: {}", port, strerror(errno));
        close(portInfo.socket_fd);
        return false;
    }

    #ifdef SO_REUSEPORT
    if (setsockopt(portInfo.socket_fd, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse)) < 0) {
        SPDLOG_WARN("Failed to enable SO_REUSEPORT for port {}: {}", port, strerror(errno));
    }
    #endif

    memset(&portInfo.localAddr, 0, sizeof(portInfo.localAddr));
    portInfo.localAddr.sin_family = AF_INET;
    portInfo.localAddr.sin_port = htons(port);
    portInfo.localAddr.sin_addr.s_addr = INADDR_ANY;

    if (bind(portInfo.socket_fd, (struct sockaddr*)&portInfo.localAddr, sizeof(portInfo.localAddr)) < 0) {
        SPDLOG_ERROR("Failed to bind socket for port {}: {}", port, strerror(errno));
        close(portInfo.socket_fd);
        return false;
    }

    return true;
}

void UDPServer::sendLoop(int port) {
    auto it = ports.find(port);
    if (it == ports.end()) return;

    PortInfo& portInfo = it->second;

    if (portInfo.type != PortType::SEND) {
        SPDLOG_ERROR("sendLoop called on non-send port {}", port);
        return;
    }

    SPDLOG_DEBUG("Starting send loop for port {}", port);

    while (running) {
        std::unique_lock<std::mutex> lock(outgoingQueueMutex);
        outgoingQueueCV.wait_for(lock, std::chrono::milliseconds(100),
            [this]{ return !outgoingQueue.empty() || !running; });

        if (!running) break;

        while (!outgoingQueue.empty()) {
            UDPMessage msg = outgoingQueue.front();
            outgoingQueue.pop();
            lock.unlock();

            if (msg.port == port) {
                // Copy client state under lock, then release before sendto
                bool connected;
                std::string targetIP;
                int targetPort;
                {
                    std::lock_guard<std::mutex> clientLock(clientMutex);
                    connected = clientConnected.load();
                    targetIP = clientIP;
                    targetPort = clientPort;
                }

                // Use fixed destination port if configured, otherwise use dynamic clientPort
                if (portInfo.destinationPort != 0) {
                    targetPort = portInfo.destinationPort;
                }

                if (connected) {
                    struct sockaddr_in targetAddr;
                    memset(&targetAddr, 0, sizeof(targetAddr));
                    targetAddr.sin_family = AF_INET;
                    targetAddr.sin_port = htons(targetPort);
                    targetAddr.sin_addr.s_addr = inet_addr(targetIP.c_str());

                    ssize_t sent = sendto(portInfo.socket_fd,
                                         msg.message.c_str(),
                                         msg.message.length(),
                                         0,
                                         (struct sockaddr*)&targetAddr,
                                         sizeof(targetAddr));

                    if (sent < 0) {
                        SPDLOG_ERROR("Failed to send message on port {} to {}: {}", port, targetIP, strerror(errno));
                    } else {
                        SPDLOG_DEBUG("Sent message on port {} to {}: {}", port, targetIP, msg.message);
                    }
                } else {
                    SPDLOG_DEBUG("No client connected, discarding message on port {}", port);
                }
            } else {
                // Wrong port, put it back at the end of the queue
                std::lock_guard<std::mutex> queueLock(outgoingQueueMutex);
                outgoingQueue.push(msg);
            }

            lock.lock();
        }
        lock.unlock();
    }
}

void UDPServer::receiveLoop(int port) {
    auto it = ports.find(port);
    if (it == ports.end()) return;

    PortInfo& portInfo = it->second;

    if (portInfo.type != PortType::LISTEN) {
        SPDLOG_ERROR("receiveLoop called on non-listen port {}", port);
        return;
    }

    char buffer[4096];
    SPDLOG_DEBUG("Starting receive loop for port {}", port);

    while (running) {
        // Check for client timeout
        {
            std::lock_guard<std::mutex> lock(clientMutex);
            if (clientConnected) {
                auto now = std::chrono::steady_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - clientLastSeen).count();
                if (elapsed > clientTimeoutMs) {
                    SPDLOG_INFO("Client {}:{} timed out after {}ms, disconnecting", clientIP, clientPort, elapsed);
                    clientConnected = false;
                    clientIP.clear();
                    clientPort = 0;
                }
            }
        }

        struct sockaddr_in senderAddr;
        socklen_t senderLen = sizeof(senderAddr);

        ssize_t received = recvfrom(portInfo.socket_fd,
                                   buffer,
                                   sizeof(buffer) - 1,
                                   MSG_DONTWAIT,
                                   (struct sockaddr*)&senderAddr,
                                   &senderLen);

        if (received > 0) {
            buffer[received] = '\0';
            std::string message(buffer, received);
            std::string senderIP = inet_ntoa(senderAddr.sin_addr);

            // Handle client connection
            {
                std::lock_guard<std::mutex> lock(clientMutex);
                if (!clientConnected) {
                    // Accept new client
                    clientIP = senderIP;
                    clientPort = ntohs(senderAddr.sin_port);
                    clientConnected = true;
                    clientLastSeen = std::chrono::steady_clock::now();
                    SPDLOG_INFO("Client connected: {}:{}", clientIP, clientPort);
                } else if (clientIP == senderIP) {
                    // Update last seen and port for existing client
                    clientPort = ntohs(senderAddr.sin_port);
                    clientLastSeen = std::chrono::steady_clock::now();
                } else {
                    // Reject message from different client while one is already connected
                    SPDLOG_DEBUG("Ignoring message from {} (client {} is already connected)",
                                senderIP, clientIP);
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    continue;
                }
            }

            // Add to incoming queue
            {
                std::lock_guard<std::mutex> lock(incomingQueueMutex);
                incomingQueue.push(UDPMessage(port, message, senderIP));
            }

            SPDLOG_DEBUG("Received message on port {} from {}: {}", port, senderIP, message);
        } else if (received < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            if (running) {
                SPDLOG_ERROR("Error receiving on port {}: {}", port, strerror(errno));
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void UDPServer::closeSocket(PortInfo& portInfo) {
    if (portInfo.socket_fd >= 0) {
        close(portInfo.socket_fd);
        portInfo.socket_fd = -1;
    }
}

bool UDPServer::sendMessage(int port, const std::string& message) {
    auto it = ports.find(port);
    if (it == ports.end()) {
        SPDLOG_WARN("Attempted to send message to unknown port {}", port);
        return false;
    }

    if (it->second.type != PortType::SEND) {
        SPDLOG_WARN("sendMessage() can only be used on SEND ports, port {} is LISTEN", port);
        return false;
    }

    if (!clientConnected) {
        SPDLOG_DEBUG("No client connected, not queuing message for port {}", port);
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(outgoingQueueMutex);
        outgoingQueue.push(UDPMessage(port, message));
    }
    outgoingQueueCV.notify_all();
    return true;
}

bool UDPServer::getIncomingMessage(UDPMessage& msg) {
    std::lock_guard<std::mutex> lock(incomingQueueMutex);
    if (incomingQueue.empty()) {
        return false;
    }

    msg = incomingQueue.front();
    incomingQueue.pop();
    return true;
}

bool UDPServer::hasIncomingMessages() const {
    std::lock_guard<std::mutex> lock(incomingQueueMutex);
    return !incomingQueue.empty();
}

bool UDPServer::isClientConnected() const {
    std::lock_guard<std::mutex> lock(clientMutex);
    return clientConnected;
}

std::string UDPServer::getClientIP() const {
    std::lock_guard<std::mutex> lock(clientMutex);
    return clientIP;
}

void UDPServer::disconnectClient() {
    std::lock_guard<std::mutex> lock(clientMutex);
    if (clientConnected) {
        SPDLOG_INFO("Disconnecting client {}:{}", clientIP, clientPort);
        clientConnected = false;
        clientIP.clear();
        clientPort = 0;
    }
}
