//! @file serial.cpp
//! @brief Serial implementation: termios setup, session start/stop and the
//!        RX/TX thread loops.
//!
//! The API is documented in serial.h.

#include "serial.h"

#include <cstring>

Serial::~Serial() {
    end();
}

int Serial::configurePort(speed_t baudRate) {
    struct termios tty;

    if (tcgetattr(portFd, &tty) != 0) {
        SPDLOG_ERROR("Error from tcgetattr: {}", strerror(errno));
        return -1;
    }

    cfsetispeed(&tty, baudRate);
    cfsetospeed(&tty, baudRate);

    tty.c_cflag &= ~PARENB;          // No parity
    tty.c_cflag &= ~CSTOPB;          // 1 stop bit
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;              // 8 bits per byte
    tty.c_cflag &= ~CRTSCTS;         // No hardware flow control
    tty.c_cflag |= CREAD | CLOCAL;   // Enable read, ignore modem ctrl lines

    //! Raw input: no line editing, echo or signal characters
    tty.c_lflag &= ~ICANON;          // Disable canonical mode (raw input)
    tty.c_lflag &= ~ECHO;
    tty.c_lflag &= ~ECHOE;
    tty.c_lflag &= ~ECHONL;
    tty.c_lflag &= ~ISIG;

    //! No software flow control and no translation of input bytes (CR/LF,
    //! breaks, parity marks, 8th bit)
    tty.c_iflag &= ~(IXON | IXOFF | IXANY);
    tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL);

    //! No output processing (no LF -> CRLF)
    tty.c_oflag &= ~OPOST;
    tty.c_oflag &= ~ONLCR;

    tty.c_cc[VTIME] = 0;             // No timeout — block until data arrives
    tty.c_cc[VMIN]  = 1;             // Block until at least 1 byte is available

    if (tcsetattr(portFd, TCSANOW, &tty) != 0) {
        SPDLOG_ERROR("Error from tcsetattr: {}", strerror(errno));
        return -1;
    }

    return 0;
}

int Serial::openPort(const char* portName, speed_t baudRate) {
    if (portName == nullptr) {
        SPDLOG_ERROR("Port name not specified");
        return -1;
    }

    //! O_NOCTTY: the port must not become the controlling terminal
    portFd = open(portName, O_RDWR | O_NOCTTY | O_SYNC);
    if (portFd < 0) {
        SPDLOG_ERROR("Error opening {}: {}", portName, strerror(errno));
        return -1;
    }

    if (configurePort(baudRate) != 0) {
        SPDLOG_ERROR("Failed to configure port {}", portName);
        close(portFd);
        portFd = -1;
        return -1;
    }

    return 0;
}

void Serial::closePort() {
    if (portFd >= 0) {
        close(portFd);
        portFd = -1;
    }
}

int Serial::readSer(void* buffer, size_t nBytes) const {
    int n = read(portFd, buffer, nBytes);
    if (n < 0) {
        SPDLOG_ERROR("Read error: {}", strerror(errno));
        return -1;
    }
    return n;
}

int Serial::writeSer(const void* buffer, size_t nBytes) {
    int n = write(portFd, buffer, nBytes);
    if (n < 0) {
        SPDLOG_ERROR("Write error: {}", strerror(errno));
        return -1;
    }
    return n;
}

void Serial::clearReadBuffer() {
    tcflush(portFd, TCIOFLUSH);
    //! The RX thread drops its partial line when it handles its next byte
    dropPartialLine = true;
    std::lock_guard<std::mutex> lock(receiveMutex);
    serialReceive.clear();
}

int Serial::setBaudRate(speed_t baudRate) {
    if (portFd < 0 || !running.load()) return -1;
    //! Change only the speed; the raw setup made by configurePort() is kept
    struct termios tty;
    if (tcgetattr(portFd, &tty) != 0) {
        SPDLOG_ERROR("Error from tcgetattr: {}", strerror(errno));
        return -1;
    }
    cfsetispeed(&tty, baudRate);
    cfsetospeed(&tty, baudRate);
    if (tcsetattr(portFd, TCSANOW, &tty) != 0) {
        SPDLOG_ERROR("Error from tcsetattr: {}", strerror(errno));
        return -1;
    }
    //! Bytes received at the old rate are garbage at the new one
    clearReadBuffer();
    return 0;
}

int Serial::begin(const char* portName, speed_t baudRate) {
    //! End the previous session first. This also reaps threads left over from
    //! a session whose RX thread stopped on its own: assigning to a joinable
    //! std::thread would terminate.
    end();
    {
        //! Drop anything queued for the previous session
        std::lock_guard<std::mutex> lock(transmitMutex);
        std::queue<std::string>().swap(serialTransmit);
    }

    if (openPort(portName, baudRate) < 0) {
        return -1;
    }

    //! Set before the threads start, or their loops would exit at once
    running = true;
    rxThread = std::thread(&Serial::receiveLoop, this);
    txThread = std::thread(&Serial::transmitLoop, this);

    return 0;
}

void Serial::stopThreads() {
    {
        std::lock_guard<std::mutex> lock(transmitMutex);
        running = false;
    }
    transmitCv.notify_all();
}

void Serial::end() {
    stopThreads();

    //! Join before closing: the threads use portFd until they exit
    if (rxThread.joinable()) rxThread.join();
    if (txThread.joinable()) txThread.join();

    closePort();
}

void Serial::sendData(const std::string& data) {
    {
        std::lock_guard<std::mutex> lock(transmitMutex);
        serialTransmit.push(data);
    }
    transmitCv.notify_one();
}

bool Serial::getReceived(std::string& data) {
    std::lock_guard<std::mutex> lock(receiveMutex);
    if (serialReceive.empty()) {
        return false;
    }
    data = serialReceive;
    serialReceive.clear();
    return true;
}

void Serial::receiveLoop() {
    std::string line;
    char c;
    //! Polling structure, set to return when data is available to be read
    struct pollfd pfd{portFd, POLLIN, 0};

    while (running) {
        int ready = poll(&pfd, 1, 100); // wait up to 100ms for data
        if (ready < 0) {
            //! poll() failed: end the session, so isOpen() reports it
            SPDLOG_ERROR("poll error: {}", strerror(errno));
            stopThreads();
            break;
        }
        if (ready == 0) {
            continue; // timeout — check running and poll again
        }

        //! One byte at a time. poll() also returns on a hang-up or an error,
        //! which read() then reports.
        int n = readSer(&c, 1);
        if (n <= 0) {
            //! -1: read error; 0: EOF, the port hung up (device unplugged)
            if (n == 0) SPDLOG_ERROR("Serial port hung up");
            stopThreads();
            break;
        }

        //! clearReadBuffer() was called: the partial line holds older bytes
        if (dropPartialLine.exchange(false)) line.clear();
        //! LF ends a line (empty lines are skipped); any other byte, CR
        //! included, is part of the line
        if (c == '\n') {
            if (!line.empty()) {
                //! Feed the tap first: serialReceive only keeps the latest line,
                //! so this is the only place that sees every line.
                std::string clean = line;
                while (!clean.empty() && clean.back() == '\r') clean.pop_back();
                LineTap tap;
                {
                    std::lock_guard<std::mutex> tl(tapMutex);
                    tap = rxTap;
                }
                if (tap && !clean.empty()) tap(clean);

                std::lock_guard<std::mutex> lock(receiveMutex);
                serialReceive = line;
                line.clear();
            }
        } else {
            line += c;
        }
    }
}

void Serial::transmitLoop() {
    while (running) {
        //! Sleep until a string is queued or the session stops
        std::unique_lock<std::mutex> lock(transmitMutex);
        transmitCv.wait(lock, [this] {
            return !serialTransmit.empty() || !running;
        });

        //! Send everything queued (even after a stop request) before checking
        //! `running` again
        while (!serialTransmit.empty()) {
            std::string msg = serialTransmit.front();
            serialTransmit.pop();
            //! Write without the lock, so sendData() does not wait for the port
            lock.unlock();
            if (writeSer(msg.c_str(), msg.size()) >= 0) {
                LineTap tap;
                {
                    std::lock_guard<std::mutex> tl(tapMutex);
                    tap = txTap;
                }
                if (tap) {
                    std::string clean = msg;
                    while (!clean.empty() && (clean.back() == '\n' || clean.back() == '\r')) clean.pop_back();
                    tap(clean);
                }
            }
            lock.lock();
        }
    }
}

void Serial::setRxTap(LineTap tap) {
    std::lock_guard<std::mutex> lock(tapMutex);
    rxTap = std::move(tap);
}

void Serial::setTxTap(LineTap tap) {
    std::lock_guard<std::mutex> lock(tapMutex);
    txTap = std::move(tap);
}
