#include <string>
#include <thread>
#include <atomic>
#include <unordered_map>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <spdlog/spdlog.h>

#ifndef SERVERMODULE_H
#define SERVERMODULE_H

// Port type enumeration
enum class PortType {
    RECEIVE_ONLY,   // Only receives messages, no sending
    BROADCAST       // Broadcasts messages (periodic if delay > 0, otherwise on-demand via queue)
};

// Structure for UDP messages with port information
struct UDPMessage {
    int port;
    std::string message;
    std::string sender_ip;  // For incoming messages
    
    // Default constructor
    UDPMessage() : port(0) {}
    
    // Parameterized constructor
    UDPMessage(int p, const std::string& msg, const std::string& ip = "")
        : port(p), message(msg), sender_ip(ip) {}
};

class UDPServer {
public:
    UDPServer();
    ~UDPServer();
    
    // Add a broadcast port (periodically broadcasts if delayMs > 0, otherwise on-demand via queue)
    bool addBroadcastPort(int port, const std::string& message, int delayMs = 0);
    
    // Add a receive-only port (only receives messages, no sending)
    bool addReceiveOnlyPort(int port);
    
    // Remove a port
    bool removePort(int port);
    
    // Update message for a broadcast port (only works for BROADCAST ports)
    bool updateMessage(int port, const std::string& message);
    
    // Start the server (begins broadcasting on all ports)
    bool start();
    
    // Stop the server
    void stop();
    
    // Check if server is running
    bool isRunning() const { return running; }
    
    // Set broadcast interval (milliseconds) - used as default for broadcast ports
    void setBroadcastInterval(int intervalMs) { broadcastInterval = intervalMs; }
    
    // Queue access methods
    // Send a message to a specific port (adds to outgoing queue)
    bool sendMessage(int port, const std::string& message);
    
    // Get incoming message from queue (returns false if queue is empty)
    bool getIncomingMessage(UDPMessage& msg);
    
    // Check if there are incoming messages
    bool hasIncomingMessages() const;

private:
    struct PortInfo {
        int socket_fd;
        struct sockaddr_in addr;
        struct sockaddr_in localAddr;  // For binding
        std::string message;  // Only used for BROADCAST ports
        int delayMs;          // Broadcast delay in milliseconds (0 = on-demand only, >0 = periodic)
        PortType type;
        std::thread mainThread;  // broadcastLoop for BROADCAST, receiveLoop for RECEIVE_ONLY
    };
    
    std::unordered_map<int, PortInfo> ports;
    std::atomic<bool> running;
    int broadcastInterval; // milliseconds (default for broadcast ports)
    
    // Thread-safe queues
    mutable std::mutex incomingQueueMutex;
    std::queue<UDPMessage> incomingQueue;
    
    mutable std::mutex outgoingQueueMutex;
    std::queue<UDPMessage> outgoingQueue;
    std::condition_variable outgoingQueueCV;  // Notifies broadcast threads when messages are queued
    
    // Create socket for a specific port
    bool createSocket(int port, PortInfo& portInfo);
    
    // Broadcast thread function for BROADCAST ports (periodic if delay > 0, otherwise on-demand)
    void broadcastLoop(int port);
    
    // Receive thread function for RECEIVE_ONLY ports
    void receiveLoop(int port);
    
    // Check if an IP address is a local address (prevents receiving own messages)
    bool isLocalIP(const std::string& ip) const;
    
    // Close socket for a port
    void closeSocket(PortInfo& portInfo);
};

#endif // SERVERMODULE_H