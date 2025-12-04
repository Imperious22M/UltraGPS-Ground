#include "serverModule.h"
#include <chrono>
#include <thread>
#include <unistd.h>
#include <cstring>
#include <cerrno>

UDPServer::UDPServer() : running(false), broadcastInterval(1000) {
    // Constructor
}

UDPServer::~UDPServer() {
    stop();
}

bool UDPServer::addPort(int port, const std::string& message) {
    if (ports.find(port) != ports.end()) {
        SPDLOG_WARN("Port {} already exists", port);
        return false;
    }
    
    PortInfo portInfo;
    portInfo.message = message;
    
    if (!createSocket(port, portInfo)) {
        SPDLOG_ERROR("Failed to create socket for port {}", port);
        return false;
    }
    
    ports[port] = std::move(portInfo);
    SPDLOG_INFO("Added UDP broadcast on port {} with message: '{}'", port, message);
    
    // If server is already running, start broadcasting on this port
    if (running) {
        ports[port].broadcastThread = std::thread(&UDPServer::broadcastLoop, this, port);
    }
    
    return true;
}

bool UDPServer::removePort(int port) {
    auto it = ports.find(port);
    if (it == ports.end()) {
        SPDLOG_WARN("Port {} not found", port);
        return false;
    }
    
    // Stop the broadcast thread for this port
    if (it->second.broadcastThread.joinable()) {
        it->second.broadcastThread.join();
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
    
    it->second.message = message;
    SPDLOG_DEBUG("Updated message for port {} to: '{}'", port, message);
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
    
    // Start broadcast threads for all ports
    for (auto& [port, portInfo] : ports) {
        portInfo.broadcastThread = std::thread(&UDPServer::broadcastLoop, this, port);
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
        if (portInfo.broadcastThread.joinable()) {
            portInfo.broadcastThread.join();
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
    
    // Set up address structure for network broadcast
    memset(&portInfo.addr, 0, sizeof(portInfo.addr));
    portInfo.addr.sin_family = AF_INET;
    portInfo.addr.sin_port = htons(port);
    portInfo.addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    
    return true;
}

void UDPServer::broadcastLoop(int port) {
    auto it = ports.find(port);
    if (it == ports.end()) {
        return;
    }
    
    PortInfo& portInfo = it->second;
    
    //SPDLOG_DEBUG("Starting broadcast loop for port {}", port);
    
    while (running) {
        // Send the message
        const std::string& message = portInfo.message;
        ssize_t sent = sendto(portInfo.socket_fd, 
                             message.c_str(), 
                             message.length(), 
                             0,
                             (struct sockaddr*)&portInfo.addr, 
                             sizeof(portInfo.addr));
        
        if (sent < 0) {
            SPDLOG_ERROR("Failed to send on port {}: {}", port, strerror(errno));
        }

        // Wait for next broadcast
        std::this_thread::sleep_for(std::chrono::milliseconds(broadcastInterval));
    }
    
}

void UDPServer::closeSocket(PortInfo& portInfo) {
    if (portInfo.socket_fd >= 0) {
        close(portInfo.socket_fd);
        portInfo.socket_fd = -1;
    }
}