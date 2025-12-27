#include "serverModule.h"
#include "spdlog/spdlog.h"
#include <chrono>
#include <thread>
#include <unistd.h>
#include <cstring>
#include <cerrno>
#include <fcntl.h>
#include <ifaddrs.h>
#include <netdb.h>

UDPServer::UDPServer() : running(false), broadcastInterval(1000) {
    // Constructor
}

UDPServer::~UDPServer() {
    stop();
}

bool UDPServer::addBroadcastPort(int port, const std::string& message, int delayMs) {
    if (ports.find(port) != ports.end()) {
        SPDLOG_WARN("Port {} already exists", port);
        return false;
    }
    
    PortInfo portInfo;
    portInfo.type = PortType::BROADCAST;
    portInfo.message = message;
    portInfo.delayMs = delayMs;
    
    if (!createSocket(port, portInfo)) {
        SPDLOG_ERROR("Failed to create socket for port {}", port);
        return false;
    }
    
    ports[port] = std::move(portInfo);
    if (delayMs > 0) {
        SPDLOG_INFO("Added UDP broadcast port {} with periodic broadcast (delay: {}ms), message: '{}'", port, delayMs, message);
    } else {
        SPDLOG_INFO("Added UDP broadcast port {} with on-demand broadcast, message: '{}'", port, message);
    }
    
    // If server is already running, start broadcasting on this port
    if (running) {
        ports[port].mainThread = std::thread(&UDPServer::broadcastLoop, this, port);
    }
    
    return true;
}

bool UDPServer::addReceiveOnlyPort(int port) {
    if (ports.find(port) != ports.end()) {
        SPDLOG_WARN("Port {} already exists", port);
        return false;
    }
    
    PortInfo portInfo;
    portInfo.type = PortType::RECEIVE_ONLY;
    portInfo.message = "";  // Not used for receive-only ports
    portInfo.delayMs = 0;   // Not used for receive-only ports
    
    if (!createSocket(port, portInfo)) {
        SPDLOG_ERROR("Failed to create socket for port {}", port);
        return false;
    }
    
    ports[port] = std::move(portInfo);
    SPDLOG_INFO("Added UDP receive-only port {}", port);
    
    // If server is already running, start receive loop on this port
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
    
    // Stop the main thread for this port
    if (it->second.mainThread.joinable()) {
        it->second.mainThread.join();
    }
    
    closeSocket(it->second);
    ports.erase(it);
    
    SPDLOG_INFO("Removed UDP broadcast on port {}", port);
    return true;
}

bool UDPServer::updateMessage(int port, const std::string& message) {
    auto it = ports.find(port);
    if (it == ports.end()) {
        SPDLOG_WARN("Port {} not found", port);
        return false;
    }
    
    if (it->second.type != PortType::BROADCAST) {
        SPDLOG_WARN("updateMessage() can only be used on BROADCAST ports, port {} is RECEIVE_ONLY", port);
        return false;
    }
    
    it->second.message = message;
    SPDLOG_DEBUG("Updated message for broadcast port {} to: '{}'", port, message);
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
    
    // Start appropriate threads for each port based on type
    for (auto& [port, portInfo] : ports) {
        if (portInfo.type == PortType::BROADCAST) {
            portInfo.mainThread = std::thread(&UDPServer::broadcastLoop, this, port);
        } else if (portInfo.type == PortType::RECEIVE_ONLY) {
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
    
    // Wait for all threads to finish and cleanup
    for (auto& [port, portInfo] : ports) {
        if (portInfo.mainThread.joinable()) {
            portInfo.mainThread.join();
        }
        closeSocket(portInfo);
    }
    
    SPDLOG_INFO("UDP server stopped");
}

bool UDPServer::createSocket(int port, PortInfo& portInfo) {
    // Create UDP socket
    portInfo.socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (portInfo.socket_fd < 0) {
        SPDLOG_ERROR("Failed to create socket for port {}: {}", port, strerror(errno));
        return false;
    }
    
    // Enable broadcast
    int broadcast = 1;
    if (setsockopt(portInfo.socket_fd, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast)) < 0) {
        SPDLOG_ERROR("Failed to enable broadcast for port {}: {}", port, strerror(errno));
        close(portInfo.socket_fd);
        return false;
    }
    
    // Enable socket reuse to allow binding to same port
    int reuse = 1;
    if (setsockopt(portInfo.socket_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        SPDLOG_ERROR("Failed to enable socket reuse (SO_REUSEADDR) for port {}: {}", port, strerror(errno));
        close(portInfo.socket_fd);
        return false;
    }
    
    // Enable SO_REUSEPORT to allow multiple programs to bind to the same port
    // This allows other programs on localhost to also receive from this port
    #ifdef SO_REUSEPORT
    if (setsockopt(portInfo.socket_fd, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse)) < 0) {
        SPDLOG_WARN("Failed to enable SO_REUSEPORT for port {} (may not be supported on this system): {}", port, strerror(errno));
        // Don't fail if SO_REUSEPORT is not supported, SO_REUSEADDR should still work
    } else {
        SPDLOG_DEBUG("SO_REUSEPORT enabled for port {}, allowing port sharing with other programs", port);
    }
    #else
    SPDLOG_DEBUG("SO_REUSEPORT not available on this system, using SO_REUSEADDR only");
    #endif
    
    // Set up local address structure for binding (to receive messages)
    memset(&portInfo.localAddr, 0, sizeof(portInfo.localAddr));
    portInfo.localAddr.sin_family = AF_INET;
    portInfo.localAddr.sin_port = htons(port);
    portInfo.localAddr.sin_addr.s_addr = INADDR_ANY;  // Listen on all interfaces
    
    // Bind socket to local address
    if (bind(portInfo.socket_fd, (struct sockaddr*)&portInfo.localAddr, sizeof(portInfo.localAddr)) < 0) {
        SPDLOG_ERROR("Failed to bind socket for port {}: {}", port, strerror(errno));
        close(portInfo.socket_fd);
        return false;
    }
    
    // Set up address structure for network broadcast (for sending)
    memset(&portInfo.addr, 0, sizeof(portInfo.addr));
    portInfo.addr.sin_family = AF_INET;
    portInfo.addr.sin_port = htons(port);
    portInfo.addr.sin_addr.s_addr = inet_addr("255.255.255.255");  // Broadcast address
    
    return true;
}

void UDPServer::broadcastLoop(int port) {
    auto it = ports.find(port);
    if (it == ports.end()) {
        return;
    }
    
    PortInfo& portInfo = it->second;
    
    if (portInfo.type != PortType::BROADCAST) {
        SPDLOG_ERROR("broadcastLoop called on non-broadcast port {}", port);
        return;
    }
    
    if (portInfo.delayMs > 0) {
        SPDLOG_DEBUG("Starting periodic broadcast loop for port {} (delay: {}ms)", port, portInfo.delayMs);
    } else {
        SPDLOG_DEBUG("Starting on-demand broadcast loop for port {}", port);
    }
    
    while (running) {
        bool messageSent = false;
        
        // Check outgoing queue for messages to send (on-demand)
        std::unique_lock<std::mutex> lock(outgoingQueueMutex);
        if (!outgoingQueue.empty()) {
            UDPMessage msg = outgoingQueue.front();
            outgoingQueue.pop();
            lock.unlock();
            
            // Send message from queue if it's for this port
            if (msg.port == port) {
                struct sockaddr_in targetAddr;
                memset(&targetAddr, 0, sizeof(targetAddr));
                targetAddr.sin_family = AF_INET;
                targetAddr.sin_port = htons(port);
                
                // If sender_ip is provided, send to that IP, otherwise broadcast
                if (!msg.sender_ip.empty()) {
                    targetAddr.sin_addr.s_addr = inet_addr(msg.sender_ip.c_str());
                } else {
                    targetAddr.sin_addr.s_addr = inet_addr("255.255.255.255");
                }
                
                ssize_t sent = sendto(portInfo.socket_fd,
                                     msg.message.c_str(),
                                     msg.message.length(),
                                     0,
                                     (struct sockaddr*)&targetAddr,
                                     sizeof(targetAddr));
                
                if (sent < 0) {
                    SPDLOG_ERROR("Failed to send queued message on port {}: {}", port, strerror(errno));
                } else {
                    SPDLOG_DEBUG("Sent queued message on port {}: {}", port, msg.message);
                }
                messageSent = true;
            } else {
                // Wrong port, put it back at the end of the queue
                outgoingQueue.push(msg);
            }
        } else {
            lock.unlock();
        }
        
        // If delay > 0, do periodic broadcasts
        if (portInfo.delayMs > 0) {
            // Send the periodic broadcast message
            const std::string& message = portInfo.message;
            ssize_t sent = sendto(portInfo.socket_fd, 
                                 message.c_str(), 
                                 message.length(), 
                                 0,
                                 (struct sockaddr*)&portInfo.addr, 
                                 sizeof(portInfo.addr));
            if (sent < 0) {
                SPDLOG_ERROR("Failed to send broadcast on port {}: {}", port, strerror(errno));
            }
            
            // Wait for next broadcast
            std::this_thread::sleep_for(std::chrono::milliseconds(portInfo.delayMs));
        } else {
            //! Delay 1ms to prevent busy waiting
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

    }
}

void UDPServer::receiveLoop(int port) {
    auto it = ports.find(port);
    if (it == ports.end()) {
        return;
    }
    
    PortInfo& portInfo = it->second;
    
    if (portInfo.type != PortType::RECEIVE_ONLY) {
        SPDLOG_ERROR("receiveLoop called on non-receive-only port {}", port);
        return;
    }
    
    char buffer[4096];
    SPDLOG_DEBUG("Starting receive loop for receive-only port {}", port);
    
    while (running) {
        struct sockaddr_in senderAddr;
        socklen_t senderLen = sizeof(senderAddr);
        
        // Receive message (non-blocking)
        ssize_t received = recvfrom(portInfo.socket_fd,
                                   buffer,
                                   sizeof(buffer) - 1,
                                   MSG_DONTWAIT,  // Non-blocking
                                   (struct sockaddr*)&senderAddr,
                                   &senderLen);
        
        if (received > 0) {
            buffer[received] = '\0';
            std::string message(buffer, received);
            std::string senderIP = inet_ntoa(senderAddr.sin_addr);
            
            // Add to incoming queue
            {
                std::lock_guard<std::mutex> lock(incomingQueueMutex);
                incomingQueue.push(UDPMessage(port, message, senderIP));
            }
            
            SPDLOG_DEBUG("Received message on port {} from {}: {}", port, senderIP, message);
        } else if (received < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            // Error occurred (but not just "would block")
            if (running) {  // Only log if we're still supposed to be running
                SPDLOG_ERROR("Error receiving on port {}: {}", port, strerror(errno));
            }
        }
        
        // Small sleep to prevent busy waiting
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
    
    if (it->second.type != PortType::BROADCAST) {
        SPDLOG_WARN("sendMessage() can only be used on BROADCAST ports, port {} is RECEIVE_ONLY", port);
        return false;
    }
    
    std::lock_guard<std::mutex> lock(outgoingQueueMutex);
    outgoingQueue.push(UDPMessage(port, message));
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

bool UDPServer::isLocalIP(const std::string& ip) const {
    // Check for loopback address
    //! This will break receive functionality in localhost use-case
    if (ip == "127.0.0.1" || ip == "::1" || ip == "localhost") {
        return true;
    }
    
    // Get all local network interfaces and check if the IP matches
    struct ifaddrs *ifaddr, *ifa;
    if (getifaddrs(&ifaddr) == -1) {
        SPDLOG_WARN("Failed to get network interfaces: {}", strerror(errno));
        return false;
    }
    
    bool isLocal = false;
    for (ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == nullptr) {
            continue;
        }
        
        // Check IPv4 addresses
        if (ifa->ifa_addr->sa_family == AF_INET) {
            struct sockaddr_in* sin = (struct sockaddr_in*)ifa->ifa_addr;
            char localIP[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &(sin->sin_addr), localIP, INET_ADDRSTRLEN);
            
            if (ip == localIP) {
                isLocal = true;
                break;
            }
        }
        // Check IPv6 addresses
        else if (ifa->ifa_addr->sa_family == AF_INET6) {
            struct sockaddr_in6* sin6 = (struct sockaddr_in6*)ifa->ifa_addr;
            char localIP[INET6_ADDRSTRLEN];
            inet_ntop(AF_INET6, &(sin6->sin6_addr), localIP, INET6_ADDRSTRLEN);
            
            if (ip == localIP) {
                isLocal = true;
                break;
            }
        }
    }
    
    freeifaddrs(ifaddr);
    return isLocal;
}