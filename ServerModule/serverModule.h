//! @file serverModule.h
//! @brief Network server of the ground station: TCP command port and UDP
//!        continuous-data stream.
//!
//! Control programs (such as UltraGPS-Control) connect to the TCP port (9000 by
//! default) to send base-station commands and read the replies. While the
//! base-station is in continuous mode, the main loop hands its readings to
//! sendContinuous(), and they go out as UDP datagrams (port 9001 by default) to
//! every peer that registered by sending a datagram to that port.
//!
//! Threads: start() starts four worker threads (TCP accept, TCP TX, UDP RX,
//! UDP TX) and the accept thread starts one RX thread per TCP client; stop()
//! joins them all. The main loop (main.cpp) polls receiveCommand() and answers
//! with sendCommand() (every client) or sendCommandTo() (one client). The TUI
//! thread only calls const methods (getters, getClients(), client counts).

#pragma once

#include <string>
#include <thread>
#include <atomic>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <memory>
#include <map>
#include <chrono>
#include <cstdint>
#include <utility>

//! @brief State of one TCP connection.
//!
//! Held through std::shared_ptr by ServerModule::clients_ and by the client's own
//! RX thread. The RX thread sets alive=false on disconnect (the TX thread on a
//! failed send, stop() on shutdown); the accept thread prunes and joins dead
//! handles on the next accepted connection, and stop() joins the rest.
struct ClientHandle {
    uint64_t id = 0;    //!< unique per connection (1, 2, ...), names the sender of a command
    int fd = -1;        //!< connected socket; closed by the prune pass or stop() (which sets -1)
    std::string ip;     //!< peer IPv4 address, dotted decimal
    int port = 0;       //!< peer TCP port
    std::atomic<bool> alive{true};  //!< false once the connection is dead
    std::thread rxThread;           //!< runs ServerModule::tcpClientRxLoop() for this client
    //! When the connection was accepted (reported as ClientInfo::since)
    std::chrono::steady_clock::time_point connectedAt = std::chrono::steady_clock::now();

    //! @brief Empty handle (fd -1, alive); the accept thread fills in the connection.
    ClientHandle() = default;
    //! @brief Not copyable: the handle owns its RX thread; share it through std::shared_ptr.
    ClientHandle(const ClientHandle&) = delete;
    //! @brief Not copy-assignable, for the same reason as the copy constructor.
    ClientHandle& operator=(const ClientHandle&) = delete;
};

//! @brief Snapshot of one connected peer, returned by ServerModule::getClients().
struct ClientInfo {
    std::string proto;  //!< "TCP" or "UDP"
    std::string ip;     //!< peer IPv4 address, dotted decimal
    int port = 0;       //!< peer port
    std::chrono::steady_clock::time_point since;  //!< connect time (TCP) / registration time (UDP)
};

//! @brief TCP command server and UDP continuous-data stream of the ground station.
//!
//! - TCP: a FIFO queue in each direction. Whatever a client sends is queued with
//!   the client's id for receiveCommand(); replies queued with sendCommand() (every
//!   client) or sendCommandTo() (one client) are sent by the TCP TX thread.
//! - UDP: latest-only. sendContinuous() replaces the packet waiting to be sent, and
//!   the UDP TX thread sends it to every registered peer. A peer registers by
//!   sending a datagram to the UDP port (its content is ignored) and stays
//!   registered until clearUdpClients().
//!
//! Both sockets bind INADDR_ANY; the network device only decides which address
//! getIP() reports.
//! @note Every method except start() and stop() is thread-safe. No code path holds
//!       two of the mutexes at once, so there is no lock order to keep.
class ServerModule {
public:
    //! @brief Store the ports and resolve the network device to report.
    //!
    //! Opens no socket; start() does.
    //! @param tcpPort   TCP port for commands and replies.
    //! @param udpPort   UDP port for the continuous stream.
    //! @param netDevice Interface whose IPv4 address getIP() reports (getNetDevice()
    //!                  reports its name); the sockets always bind INADDR_ANY.
    //!                  Empty = auto-detect (wireless, then wired ethernet, then any
    //!                  other up non-loopback interface; see resolveNetwork()).
    explicit ServerModule(int tcpPort, int udpPort, const std::string& netDevice);
    //! @brief Calls stop().
    ~ServerModule();

    //! @brief Open the sockets and start the worker threads.
    //!
    //! Binds the TCP listener and the UDP socket to INADDR_ANY, then starts the
    //! TCP accept, TCP TX, UDP RX and UDP TX threads.
    //! @return true when started; false if already running or a socket could not
    //!         be created, bound or put into listening mode.
    //! @note start() and stop() are not synchronised with each other: call them
    //!       from the owning thread only.
    bool start();
    //! @brief Close every socket and join every thread; does nothing if not running.
    //!
    //! Shuts down the listener (ending the accept thread), then every TCP client
    //! (ending its RX thread), then the UDP socket (ending the UDP RX thread); the
    //! TX threads are woken through their condition variables. The registered UDP
    //! peers and any received commands not yet taken are kept.
    //! @note Call from the owning thread only (see start()); the destructor calls it too.
    void stop();
    //! @brief Whether the server is running.
    //! @return true from a successful start() until stop() (not a health check of
    //!         the worker threads).
    bool isRunning() const { return running_.load(); }

    //! @name TCP commands
    //! Buffered FIFO queue in each direction. These calls only touch the queues,
    //! never a socket, so they do not wait for network I/O.
    //! @{

    //! @brief Queue a message for every connected TCP client.
    //!
    //! The TCP TX thread sends it as is (no newline is added) to every client that
    //! is alive when it is sent.
    //! @param message Data to send.
    //! @return true if queued; false if the server is not running.
    bool sendCommand(const std::string& message);
    //! @brief Queue a message for one TCP client only.
    //! @param clientId ClientHandle::id of the client, e.g. from receiveCommand();
    //!                 0 = every client, as sendCommand(). The message is dropped if
    //!                 that client is gone by the time it is sent.
    //! @param message  Data to send, as is.
    //! @return true if queued; false if the server is not running.
    bool sendCommandTo(uint64_t clientId, const std::string& message);
    //! @brief Take the oldest data received from any TCP client. Never blocks.
    //!
    //! Each entry is the data of one recv() call (at most 4095 bytes), not a parsed
    //! line: TCP keeps no message boundaries, so commands sent back to back can
    //! share an entry and one command can be split over two.
    //! @param message Receives the data; unchanged when the queue is empty.
    //! @param from    If not null, receives the sender's ClientHandle::id (for
    //!                sendCommandTo()); unchanged when the queue is empty.
    //! @return true if an entry was taken; false if the queue was empty.
    bool receiveCommand(std::string& message, uint64_t* from = nullptr);

    //! @}

    //! @name UDP continuous stream
    //! @{

    //! @brief Send a packet to every registered UDP peer (latest-only).
    //!
    //! Replaces the packet waiting to be sent, if any, which is then never sent.
    //! The UDP TX thread sends the packet as one datagram to each peer registered
    //! at that moment, or drops it if there is none. Does not wait for network I/O.
    //! @param message Datagram payload, sent as is.
    //! @return true if stored; false if the server is not running.
    bool sendContinuous(const std::string& message);

    //! @}

    //! @name Network information
    //! Resolved at construction and constant afterwards.
    //! @{

    //! @brief Address shown as the server address.
    //! @return IPv4 address of the network device; "0.0.0.0" if none was found.
    std::string getIP() const { return ip_; }
    //! @brief Network device whose address getIP() reports.
    //! @return The requested device, the auto-detected one, or "any" when
    //!         auto-detection found none.
    std::string getNetDevice() const { return netDevice_; }
    //! @brief TCP command port.
    //! @return The port given to the constructor.
    int getTcpPort() const { return tcpPort_; }
    //! @brief UDP stream port.
    //! @return The port given to the constructor.
    int getUdpPort() const { return udpPort_; }

    //! @}

    //! @name Connected peers
    //! @{

    //! @brief Snapshot of the connected peers.
    //! @return The alive TCP clients in connection order, then the registered UDP
    //!         peers sorted by IP string and port.
    //! @note Locks clientsMutex_, then udpClientsMutex_ (one at a time).
    std::vector<ClientInfo> getClients() const;
    //! @brief Number of alive TCP clients.
    //! @return Clients whose connection is up (dead handles awaiting the prune pass
    //!         are not counted).
    size_t tcpClientCount() const;
    //! @brief Number of registered UDP peers.
    //! @return Size of the UDP peer list.
    size_t udpClientCount() const;

    //! @brief Forget every registered UDP peer; peers re-register with their next datagram.
    void clearUdpClients();

    //! @}

    //! @name TCP client limit
    //! When enabled, a new connection that arrives while getMaxClients() or more
    //! clients are connected is sent "E: server full\n" and closed. Lowering the
    //! limit never drops existing sessions; it only affects new connections. UDP
    //! peers are not limited: UDP is connectionless, so there is no session to
    //! refuse or close. Default: enabled, 1 client.
    //! @{

    //! @brief Set the TCP client limit.
    //! @param n New connections are refused while at least this many clients are
    //!          connected; 0 is taken as 1.
    void setMaxClients(unsigned n) { maxClients_ = n == 0 ? 1 : n; }
    //! @brief Current TCP client limit, enforced only while the limit is enabled.
    //! @return The limit (at least 1).
    unsigned getMaxClients() const { return maxClients_.load(); }
    //! @brief Enable or disable the TCP client limit.
    //! @param on true to refuse connections beyond getMaxClients().
    void setClientLimitEnabled(bool on) { limitEnabled_ = on; }
    //! @brief Whether the TCP client limit is enforced.
    //! @return true if connections beyond getMaxClients() are refused.
    bool isClientLimitEnabled() const { return limitEnabled_.load(); }

    //! @}

private:
    int tcpPort_;           //!< TCP command port
    int udpPort_;           //!< UDP stream port
    std::string netDevice_; //!< device name reported by getNetDevice()
    std::string ip_;        //!< address reported by getIP()

    //! Set by a successful start(), cleared by stop(); the worker loops run while it is set
    std::atomic<bool> running_{false};
    std::atomic<unsigned> maxClients_{1};   //!< TCP client limit (at least 1)
    std::atomic<bool> limitEnabled_{true};  //!< whether maxClients_ is enforced

    // ---- TCP ----------------------------------------------------------------

    int tcpListenFd_ = -1;          //!< listening socket, -1 when closed
    std::thread tcpAcceptThread_;   //!< runs tcpAcceptLoop()
    std::thread tcpTxThread_;       //!< runs tcpTxLoop()

    mutable std::mutex clientsMutex_;   //!< guards clients_ (mutable: const getters lock it)
    //! Accepted TCP connections in accept order: the alive ones, plus dead ones the
    //! accept thread has not pruned yet
    std::vector<std::shared_ptr<ClientHandle>> clients_;
    uint64_t nextClientId_ = 1;     //!< next ClientHandle::id (accept thread only)

    std::mutex tcpRxMutex_;         //!< guards tcpRxQueue_
    //! Shared RX queue — all client RX threads push (sender id, packet) here and
    //! receiveCommand() pops the oldest; a packet is the data of one recv() call.
    std::queue<std::pair<uint64_t, std::string>> tcpRxQueue_;

    std::mutex tcpTxMutex_;         //!< guards tcpTxQueue_
    //! Shared TX queue — TX thread drains this and sends each packet to the
    //! client it names, or to all alive clients when the id is 0.
    std::queue<std::pair<uint64_t, std::string>> tcpTxQueue_;
    std::condition_variable tcpTxCV_;   //!< wakes the TX thread (packet queued, or stop())

    // ---- UDP ----------------------------------------------------------------

    int udpFd_ = -1;                //!< UDP socket, -1 when closed
    std::thread udpRxThread_;       //!< runs udpRxLoop()
    std::thread udpTxThread_;       //!< runs udpTxLoop()

    std::mutex udpTxMutex_;         //!< guards udpTxLatest_ and udpTxHasData_
    //! TX slot — only the most recent packet is kept; TX thread sends on update.
    std::string udpTxLatest_;
    bool udpTxHasData_ = false;     //!< udpTxLatest_ holds a packet the TX thread has not taken
    std::condition_variable udpTxCV_;   //!< wakes the UDP TX thread (packet stored, or stop())

    mutable std::mutex udpClientsMutex_;    //!< guards udpClients_ (mutable: const getters lock it)
    //! UDP peers discovered by RX, keyed by (IP, port), with the time each was first
    //! seen; the UDP TX thread sends the continuous stream to all of them.
    std::map<std::pair<std::string, int>, std::chrono::steady_clock::time_point> udpClients_;

    // ---- Thread functions ---------------------------------------------------

    //! @brief TCP accept thread: accept clients until accept() fails.
    //!
    //! For each connection: gives it the next id, prunes the dead handles (joining
    //! their RX threads outside clientsMutex_), then either refuses it with
    //! "E: server full\n" (limit reached) or starts its RX thread and adds it to
    //! clients_. stop() ends the loop by shutting the listener down; any other
    //! accept() error ends it too.
    void tcpAcceptLoop();
    //! @brief Per-client TCP RX thread: queue everything the client sends.
    //!
    //! Pushes the data of each recv() call onto tcpRxQueue_, tagged with the
    //! client's id. Ends on disconnect, on a recv() error, or once the server stops
    //! or the client is marked dead, and then sets client->alive to false; the
    //! socket is closed later by the accept thread's prune pass or by stop().
    //! @param client The connection; this thread holds one of its references.
    void tcpClientRxLoop(std::shared_ptr<ClientHandle> client);
    //! @brief TCP TX thread: send the queued packets.
    //!
    //! Sleeps on tcpTxCV_ until tcpTxQueue_ has packets, then sends each to the
    //! client it names or, for id 0, to every alive client. The queue lock is
    //! released while sending. Sends use MSG_NOSIGNAL, so a client that has gone
    //! gives an error instead of SIGPIPE (whose default action ends the process);
    //! a failed send marks the client dead and shuts its socket down. Runs until
    //! stop() clears running_.
    //! @note Holds clientsMutex_ while sending, so a send() that blocks (a client
    //!       that stopped reading) also holds up the accept thread and the
    //!       getClients() / tcpClientCount() callers.
    void tcpTxLoop();
    //! @brief UDP RX thread: register the sender of each datagram as a peer.
    //!
    //! The content is ignored; a peer already registered keeps its first-seen
    //! time. Ends when recvfrom() returns 0 or fails: stop() causes that by
    //! shutting the socket down, but an empty datagram also returns 0, so it ends
    //! the loop as well.
    void udpRxLoop();
    //! @brief UDP TX thread: send the latest packet to every registered peer.
    //!
    //! Sleeps on udpTxCV_ until sendContinuous() stores a packet, copies the peer
    //! list (so no lock is held during I/O) and sends the packet as one datagram to
    //! each peer; a failed sendto() does not unregister the peer. Returns on stop();
    //! a packet still waiting then is dropped.
    void udpTxLoop();

    // ---- Helpers ------------------------------------------------------------

    //! @brief Open the TCP listening socket on tcpPort_ (INADDR_ANY, backlog 8).
    //! @return true on success; false on failure (tcpListenFd_ is then -1).
    bool initTCP();
    //! @brief Open the UDP socket on udpPort_ (INADDR_ANY).
    //! @return true on success; false on failure (udpFd_ is then -1).
    bool initUDP();
    //! @brief Fill netDevice_ and ip_, which getNetDevice() and getIP() report.
    //!
    //! A named device keeps its name and reports its first IPv4 address.
    //! Auto-detection picks among the up, non-loopback interfaces that have an IPv4
    //! address, by name: "wl*" (wireless) first, then "eth*" / "en*" (wired), then
    //! any other; the first one getifaddrs() lists wins a tie. When nothing is
    //! found the address is "0.0.0.0", and an auto-detected name is "any".
    //! @param requestedDevice Interface name given to the constructor; empty =
    //!                        auto-detect.
    void resolveNetwork(const std::string& requestedDevice);
};
