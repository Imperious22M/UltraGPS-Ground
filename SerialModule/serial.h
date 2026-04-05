//! file: Serial — concrete serial port implementation.
//! Implements the low-level POSIX port operations for SerialInterface.

#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <poll.h>
#include <cstdint>
#include <cerrno>
#include <string>
#include <spdlog/spdlog.h>

#include <string>
#include <queue>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <atomic>
#include <termios.h>

#ifndef SERIALMODULE
#define SERIALMODULE

class Serial{
public:
    //! Constructor and destructor
    Serial();
    ~Serial();

    //! Open the port and start the RX/TX threads.
    //! Returns 0 on success, negative if the port cannot be opened.
    int begin(const char* portName, speed_t baudRate);

    //! Stop the RX/TX threads and close the port.
    int end();

    //! Push an ASCII string onto the transmit queue to be sent over serial.
    void sendData(const std::string& data);

    //! Pop one received line from the receive queue.
    //! Returns true and populates data if a line is available, false if the queue is empty.
    bool getReceived(std::string& data);

    //! Clear the serial input buffer. Returns 0 on success, negative on failure.
    int clearReadBuffer();

protected:
    //! Open and configure the physical serial port.
    //! Called by begin() before threads are started.
    //! Returns 0 on success, negative on failure.
    int openPort(const char* portName, speed_t baudRate);

    //! Close the physical serial port.
    int closePort();

    //! Write nBytes from buffer to the serial port.
    //! Returns number of bytes written, negative on error.
    int writeSer(const void* buffer, size_t nBytes);

private:
    std::queue<std::string> serialReceive;
    std::queue<std::string> serialTransmit;
    std::mutex receiveMutex;
    std::mutex transmitMutex;
    std::condition_variable transmitCv;
    std::atomic<bool> running{false};
    std::thread rxThread;
    std::thread txThread;
    const char* portName{nullptr};
    int portFd{-1};
    speed_t baudRate{B19200};

    //! Configure the serial port with the current settings. Returns 0 on success, negative on failure.
    int configurePort();

    //! Read up to nBytes from the serial port into buffer
    //! Times out after 100ms if no data is received. Returns number of bytes read, 0 on timeout, negative on error.
    int readSer(void* buffer, size_t nBytes) const;

    //! Thread functions for RX and TX loops
    void receiveLoop();
    void transmitLoop();
};

#endif
