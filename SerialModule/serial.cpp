//! file: Implementation of Serial.h

#include "serial.h"

Serial::Serial() : running(false) {}

Serial::~Serial() {
    if (running) {
        end();
    }
}

int Serial::configurePort() {
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

    tty.c_lflag &= ~ICANON;          // Disable canonical mode (raw input)
    tty.c_lflag &= ~ECHO;
    tty.c_lflag &= ~ECHOE;
    tty.c_lflag &= ~ECHONL;
    tty.c_lflag &= ~ISIG;

    tty.c_iflag &= ~(IXON | IXOFF | IXANY);
    tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL);

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
    this->portName = portName;
    this->baudRate = baudRate;

    if (portName == nullptr) {
        SPDLOG_ERROR("Port name not specified");
        return -1;
    }

    portFd = open(portName, O_RDWR | O_NOCTTY | O_SYNC);
    if (portFd < 0) {
        SPDLOG_ERROR("Error opening {}: {}", portName, strerror(errno));
        return -1;
    }

    if (configurePort() != 0) {
        SPDLOG_ERROR("Failed to configure port {}", portName);
        close(portFd);
        portFd = -1;
        return -1;
    }

    return 0;
}

int Serial::closePort() {
    if (portFd >= 0) {
        close(portFd);
        portFd = -1;
    }
    return 0;
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

int Serial::clearReadBuffer() {
    tcflush(portFd, TCIOFLUSH);
    std::lock_guard<std::mutex> lock(receiveMutex);
    serialReceive.clear();
    return 0;
}

int Serial::begin(const char* portName, speed_t baudRate) {
    if (openPort(portName, baudRate) < 0) {
        return -1;
    }

    running = true;
    rxThread = std::thread(&Serial::receiveLoop, this);
    txThread = std::thread(&Serial::transmitLoop, this);

    return 0;
}

int Serial::end() {
    running = false;
    transmitCv.notify_all();

    if (rxThread.joinable()) rxThread.join();
    if (txThread.joinable()) txThread.join();

    return closePort();
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
            SPDLOG_ERROR("poll error: {}", strerror(errno));
            running = false;
            transmitCv.notify_all();
            break;
        }
        if (ready == 0) {
            continue; // timeout — check running and poll again
        }

        int n = readSer(&c, 1);
        if (n < 0) {
            running = false;
            transmitCv.notify_all();
            break;
        }
        if (n == 0) continue;

        if (c == '\n') {
            if (!line.empty()) {
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
        std::unique_lock<std::mutex> lock(transmitMutex);
        transmitCv.wait(lock, [this] {
            return !serialTransmit.empty() || !running;
        });

        while (!serialTransmit.empty()) {
            std::string msg = serialTransmit.front();
            serialTransmit.pop();
            lock.unlock();
            writeSer(msg.c_str(), msg.size());
            lock.lock();
        }
    }
}