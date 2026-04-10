#pragma once

#include <string>
#include <thread>
#include <atomic>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <memory>
#include <set>
#include <utility>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <spdlog/spdlog.h>

// Per-TCP-client state. Owned by shared_ptr shared between the accept thread and
// the client's RX thread. The RX thread sets alive=false on disconnect; the accept
// thread prunes and joins dead handles on the next accepted connection.
struct ClientHandle {
    int fd = -1;
    std::string ip;
    int port = 0;
    std::atomic<bool> alive{true};
    std::thread rxThread;

    ClientHandle() = default;
    ClientHandle(const ClientHandle&) = delete;
    ClientHandle& operator=(const ClientHandle&) = delete;
};

class ServerModule {
public:
    // Ports default to 9000 (TCP) and 9001 (UDP).
    // netDevice: bind to a specific interface (e.g. "wlan0"). Empty = auto-detect WiFi/ethernet.
    explicit ServerModule(int tcpPort = 9000, int udpPort = 9001,
                          const std::string& netDevice = "");
    ~ServerModule();

    // Start all threads (TCP accept + TX, UDP RX + TX).
    bool start();
    void stop();
    bool isRunning() const { return running_.load(); }

    // TCP — buffered FIFO queue for both directions.
    // sendCommand: enqueue a packet; TX thread broadcasts to all connected clients.
    // receiveCommand: dequeue the next packet received from any client.
    bool sendCommand(const std::string& message);
    bool receiveCommand(std::string& message);
    bool hasPendingCommands() const;

    // UDP — latest-only TX, queued RX.
    // sendContinuous: overwrite the current TX slot; TX thread sends it as soon as possible.
    // receiveContinuous: dequeue the next received UDP packet.
    bool sendContinuous(const std::string& message);
    bool receiveContinuous(std::string& message);
    bool hasContinuousData() const;

    // Network info (resolved at construction).
    std::string getIP() const { return ip_; }
    std::string getNetDevice() const { return netDevice_; }

private:
    int tcpPort_;
    int udpPort_;
    std::string netDevice_;
    std::string ip_;

    std::atomic<bool> running_{false};

    // ---- TCP ----------------------------------------------------------------

    int tcpListenFd_ = -1;
    std::thread tcpAcceptThread_;
    std::thread tcpTxThread_;

    mutable std::mutex clientsMutex_;
    std::vector<std::shared_ptr<ClientHandle>> clients_;

    // Shared RX queue — all client RX threads push here.
    mutable std::mutex tcpRxMutex_;
    std::queue<std::string> tcpRxQueue_;

    // Shared TX queue — TX thread drains this and broadcasts to all alive clients.
    mutable std::mutex tcpTxMutex_;
    std::queue<std::string> tcpTxQueue_;
    std::condition_variable tcpTxCV_;

    // ---- UDP ----------------------------------------------------------------

    int udpFd_ = -1;
    std::thread udpRxThread_;
    std::thread udpTxThread_;

    // RX queue — receives from any UDP sender that has contacted us.
    mutable std::mutex udpRxMutex_;
    std::queue<std::string> udpRxQueue_;

    // TX slot — only the most recent packet is kept; TX thread sends on update.
    mutable std::mutex udpTxMutex_;
    std::string udpTxLatest_;
    bool udpTxHasData_ = false;
    std::condition_variable udpTxCV_;

    // UDP clients discovered by RX (send replies to all of them).
    mutable std::mutex udpClientsMutex_;
    std::set<std::pair<std::string, int>> udpClients_;

    // ---- Thread functions ---------------------------------------------------

    void tcpAcceptLoop();
    void tcpClientRxLoop(std::shared_ptr<ClientHandle> client);
    void tcpTxLoop();
    void udpRxLoop();
    void udpTxLoop();

    // ---- Helpers ------------------------------------------------------------

    bool initTCP();
    bool initUDP();
    void resolveNetwork(const std::string& requestedDevice);
};
