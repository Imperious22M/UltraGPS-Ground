//! @file serial.h
//! @brief Serial: a POSIX (termios) serial port served by an RX and a TX thread.
//!
//! An RX thread splits the input into lines (the latest one is kept for
//! getReceived()) and a TX thread writes the queued strings. Optional line taps
//! see the lines that cross the link.

#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <poll.h>
#include <cerrno>
#include <string>
#include <spdlog/spdlog.h>

#include <queue>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <atomic>
#include <functional>

#ifndef SERIALMODULE
#define SERIALMODULE

//! @brief A POSIX (termios) serial port, read and written by its own threads.
//!
//! begin() opens the port in raw 8N1 mode and starts a session: the RX thread
//! splits the input into LF-terminated lines and keeps only the latest one for
//! getReceived(); the TX thread writes the strings queued by sendData(). The
//! session lasts until end(), or until the RX thread stops it after an error
//! or a hang-up (isOpen() tells). The destructor calls end().
//! @note begin(), end(), setBaudRate() and clearReadBuffer() use the port
//!       descriptor that begin() and end() change, so call them from one
//!       controlling thread (Comms calls them from the main loop). sendData(),
//!       getReceived(), isOpen(), setRxTap() and setTxTap() are thread-safe.
class Serial{
public:
    //! @brief Callback for the lines that cross the link.
    //!
    //! Gets each line without its line ending; setRxTap() and setTxTap() say
    //! which lines. Used by the TUI to build its serial "chat log". Called from
    //! the RX/TX threads, so implementations must be thread-safe, should return
    //! quickly (the calling thread does no serial I/O meanwhile) and must not
    //! call begin() or end(), which join those threads.
    using LineTap = std::function<void(const std::string& line)>;

    //! @brief Destructor: stops the RX/TX threads and closes the port (end()).
    ~Serial();

    //! @brief Open the port and start the RX/TX threads.
    //!
    //! A session still running is ended first, and strings still in the
    //! transmit queue from an earlier session are dropped.
    //! @param portName Serial device path, e.g. `/dev/ttyACM0`.
    //! @param baudRate termios speed constant, e.g. `B38400`.
    //! @return 0 on success, -1 if the port cannot be opened or configured (the
    //!         error is logged, only with VERBOSE_LOGGING=ON).
    int begin(const char* portName, speed_t baudRate);

    //! @brief Stop the RX/TX threads and close the port.
    //!
    //! Safe to call when closed. Once woken, the TX thread writes everything
    //! queued before it checks for the stop, so end() can wait for queued
    //! strings to go out.
    void end();

    //! @brief Change the speed of the open port in place.
    //!
    //! tcsetattr() on the same file descriptor, so the modem lines are not
    //! touched (no DTR pulse, which would reset an Uno). Then clearReadBuffer(),
    //! since bytes received at the old rate are garbage at the new one: the
    //! input and output pending in the driver and the stored and partial lines
    //! are dropped. Strings still in the transmit queue are kept and go out at
    //! the new rate.
    //! @param baudRate termios speed constant, e.g. `B57600`.
    //! @return 0 on success, -1 if the session is not running or the speed
    //!         cannot be set.
    int setBaudRate(speed_t baudRate);

    //! @brief Push an ASCII string onto the transmit queue to be sent over
    //!        serial.
    //!
    //! Returns at once; the TX thread writes the string as is (the caller adds
    //! the newline). While no session is running the string stays queued until
    //! the next begin() drops it.
    //! @param data Bytes to send, e.g. `P` plus a newline.
    //! @note Thread-safe.
    void sendData(const std::string& data);

    //! @brief Take the most recently received line.
    //!
    //! Only the latest complete line is kept: a line not taken before the next
    //! one completes is lost here (the RX tap still sees it).
    //! @param data Receives the line, LF stripped and CR kept; unchanged when
    //!             there is none.
    //! @return true if a line was taken, false if no line has arrived since the
    //!         last call or clearReadBuffer().
    //! @note Thread-safe.
    bool getReceived(std::string& data);

    //! @brief Discard the input and output pending in the driver (tcflush()),
    //!        the stored line and the partial line the RX thread is assembling.
    //!
    //! The RX thread drops its partial line when it handles its next byte. The
    //! transmit queue is not touched.
    void clearReadBuffer();

    //! @brief Whether a session is running.
    //! @return true from a successful begin() until end(); goes false on its
    //!         own when the RX thread stops after a read or poll error or a
    //!         hang-up (device unplugged). The descriptor itself stays open
    //!         until end() or the next begin().
    //! @note Thread-safe.
    bool isOpen() const { return running.load(); }

    //! @brief Install the receive tap (an empty one removes it).
    //!
    //! The tap sees each received line with its trailing CRs stripped (a line
    //! left empty by that is not passed on), before getReceived() can take it.
    //! Safe to call while the port is open; the RX thread may still call the
    //! old tap once if it had already picked it up.
    //! @param tap Callback, or an empty function / nullptr for none.
    void setRxTap(LineTap tap);

    //! @brief Install the transmit tap (an empty one removes it).
    //!
    //! The tap sees each queued string once it has been written to the port
    //! (not when the write fails), with its trailing CR/LF stripped. Safe to
    //! call while the port is open; the TX thread may still call the old tap
    //! once if it had already picked it up.
    //! @param tap Callback, or an empty function / nullptr for none.
    void setTxTap(LineTap tap);

private:
    //! Latest complete line from the RX thread (empty once taken or cleared);
    //! guarded by receiveMutex
    std::string serialReceive;
    //! Strings waiting for the TX thread; guarded by transmitMutex
    std::queue<std::string> serialTransmit;
    //! Guards serialReceive
    std::mutex receiveMutex;
    //! Guards serialTransmit; stopThreads() also holds it to clear `running`
    std::mutex transmitMutex;
    //! Wakes the TX thread when a string is queued or the session stops
    std::condition_variable transmitCv;
    //! True while the session runs; the RX/TX loops end after it goes false
    std::atomic<bool> running{false};
    //! Set by clearReadBuffer(); the RX thread then drops its partial line
    std::atomic<bool> dropPartialLine{false};
    //! Runs receiveLoop()
    std::thread rxThread;
    //! Runs transmitLoop()
    std::thread txThread;
    //! Guards rxTap and txTap
    std::mutex tapMutex;
    //! Receive tap (see setRxTap())
    LineTap rxTap;
    //! Transmit tap (see setTxTap())
    LineTap txTap;
    //! File descriptor of the open port, -1 while closed
    int portFd{-1};

    //! @brief Open and configure the physical serial port (called by begin()
    //!        before the threads start).
    //! @param portName Serial device path.
    //! @param baudRate termios speed constant.
    //! @return 0 on success (portFd is set), -1 on failure (logged only with VERBOSE_LOGGING=ON).
    int openPort(const char* portName, speed_t baudRate);

    //! @brief Close the physical serial port (if open) and set portFd to -1.
    void closePort();

    //! @brief Write bytes to the serial port.
    //! @param buffer Bytes to write.
    //! @param nBytes Number of bytes in buffer.
    //! @return The number of bytes written (a short write is not retried), or
    //!         -1 on error (logged only with VERBOSE_LOGGING=ON).
    int writeSer(const void* buffer, size_t nBytes);

    //! @brief Put the open port in raw 8N1 mode at baudRate.
    //!
    //! No flow control, modem control lines ignored, no echo, signals or
    //! input/output processing; reads block until at least one byte is there
    //! (VMIN=1, VTIME=0).
    //! @param baudRate termios speed constant, for input and output.
    //! @return 0 on success, -1 on failure (logged only with VERBOSE_LOGGING=ON).
    int configurePort(speed_t baudRate);

    //! @brief Blocking read of up to nBytes (VMIN=1, VTIME=0); call it only
    //!        after poll() reports data.
    //! @param buffer Receives the bytes read.
    //! @param nBytes Size of buffer.
    //! @return The number of bytes read, 0 on EOF/hang-up, -1 on error
    //!         (logged only with VERBOSE_LOGGING=ON).
    int readSer(void* buffer, size_t nBytes) const;

    //! @brief Clear `running` and wake the TX thread.
    //!
    //! Holds transmitMutex while clearing, so the wake-up cannot be lost
    //! between the TX thread's check and its wait. Does not join: end() does
    //! that, and the RX thread calls this to end the session itself.
    void stopThreads();

    //! @brief RX thread body: read the port and split the input into lines.
    //!
    //! Each LF ends a line; empty lines are skipped. A complete line goes to
    //! the RX tap, then replaces serialReceive. poll() returns at least every
    //! 100 ms, so a stop is noticed. A poll() or read error, or EOF (hang-up),
    //! ends the session through stopThreads().
    void receiveLoop();

    //! @brief TX thread body: write the queued strings in order.
    //!
    //! Sleeps on transmitCv until a string is queued or the session stops.
    //! transmitMutex is released during each write, so sendData() never waits
    //! for the port. Each string written successfully goes to the TX tap.
    void transmitLoop();
};

#endif
