#include <vector>
#include <string>
#include <thread>
#include <atomic>
#include <unordered_map>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <spdlog/spdlog.h>

#ifndef SERVERMODULE_H
#define SERVERMODULE_H

class UDPServer {
public:
    UDPServer();
    ~UDPServer();
    
    // Add a port with its broadcast message
    bool addPort(int port, const std::string& message);
    
    // Remove a port
    bool removePort(int port);
    
    // Update message for a specific port
    bool updateMessage(int port, const std::string& message);
    
    // Start the server (begins broadcasting on all ports)
    bool start();
    
    // Stop the server
    void stop();
    
    // Check if server is running
    bool isRunning() const { return running; }
    
    // Set broadcast interval (milliseconds)
    void setBroadcastInterval(int intervalMs) { broadcastInterval = intervalMs; }

private:
    struct PortInfo {
        int socket_fd;
        struct sockaddr_in addr;
        std::string message;
        std::thread broadcastThread;
    };
    
    std::unordered_map<int, PortInfo> ports;
    std::atomic<bool> running;
    int broadcastInterval; // milliseconds
    
    // Create socket for a specific port
    bool createSocket(int port, PortInfo& portInfo);
    
    // Broadcast thread function for a specific port
    void broadcastLoop(int port);
    
    // Close socket for a port
    void closeSocket(PortInfo& portInfo);
};

#endif // SERVERMODULE_H