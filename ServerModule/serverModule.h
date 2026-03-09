#include <string>
#include <thread>
#include <atomic>
#include <unordered_map>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <spdlog/spdlog.h>

#ifndef SERVERMODULE_H
#define SERVERMODULE_H

// Port type enumeration
enum class PortType {
    LISTEN,  // Receives messages from client
    SEND     // Sends messages to connected client
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

    // Add a send port (sends to connected client on-demand)
    // destinationPort: fixed client-side port to send to (0 = use dynamic clientPort)
    bool addSendPort(int port, int destinationPort = 0);

    // Add a listen port (receives messages from client)
    bool addListenPort(int port);

    // Remove a port
    bool removePort(int port);

    // Start the server
    bool start();

    // Stop the server
    void stop();

    // Check if server is running
    bool isRunning() const { return running; }

    // Check if a client is connected
    bool isClientConnected() const;

    // Get connected client IP
    std::string getClientIP() const;

    // Disconnect the current client
    void disconnectClient();

    // Set client timeout in milliseconds (default 5000ms)
    void setClientTimeout(int timeoutMs) { clientTimeoutMs = timeoutMs; }

    // Send a message to the connected client on a specific port
    bool sendMessage(int port, const std::string& message);

    // Get incoming message from queue (returns false if queue is empty)
    bool getIncomingMessage(UDPMessage& msg);

    // Check if there are incoming messages
    bool hasIncomingMessages() const;

private:
    struct PortInfo {
        int socket_fd;
        struct sockaddr_in localAddr;  // For binding
        PortType type;
        std::thread mainThread;
        int destinationPort;  // Fixed destination port for SEND ports (0 = use dynamic clientPort)
    };

    std::unordered_map<int, PortInfo> ports;
    std::atomic<bool> running;

    // Client state
    mutable std::mutex clientMutex;
    std::string clientIP;
    int clientPort;
    std::atomic<bool> clientConnected;
    std::chrono::steady_clock::time_point clientLastSeen;
    int clientTimeoutMs;

    // Thread-safe queues
    mutable std::mutex incomingQueueMutex;
    std::queue<UDPMessage> incomingQueue;

    mutable std::mutex outgoingQueueMutex;
    std::queue<UDPMessage> outgoingQueue;
    std::condition_variable outgoingQueueCV;

    // Create socket for a specific port
    bool createSocket(int port, PortInfo& portInfo);

    // Send thread function for SEND ports
    void sendLoop(int port);

    // Receive thread function for LISTEN ports
    void receiveLoop(int port);

    // Close socket for a port
    void closeSocket(PortInfo& portInfo);
};

#endif // SERVERMODULE_H
