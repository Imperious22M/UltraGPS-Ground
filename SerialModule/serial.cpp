//! file: Implementation of the serial class

#include "serial.h"

Serial::Serial():portName(nullptr),
        portFd(-1),
        baudRate(B19200){};

int Serial::begin(const char* portName, speed_t baudRate){
    this->portName = portName;
    this->baudRate = baudRate;
    if (portName == nullptr) {
        SPDLOG_ERROR("Port name not specified");
        return -1;
    }
    //! Attempt to open the serial port
    portFd = open(portName, O_RDWR | O_NOCTTY | O_SYNC);
    if (portFd < 0) {
        SPDLOG_ERROR("Error opening {}: {}", portName, strerror(errno)); 
        return -1;
    }
    //! If the port fails to open, close the file descriptor
    if (configurePort() != 0) {
        SPDLOG_ERROR("Failed to configure port {}", portName);
        close(portFd);
        return -1;
    }

    return 0;
}

int Serial::end(){
    close(portFd);
    return 0;
}

int Serial::configurePort(){
    struct termios tty;
    //! Cygwin has an issue where it does not recognize the port
    //! as a serial device until the Arduino IDE sets it up...

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
    tty.c_cflag &= ~CRTSCTS;         // No flow control
    tty.c_cflag |= CREAD | CLOCAL;   // Enable read, ignore ctrl lines

    tty.c_lflag &= ~ICANON;          // Disable canonical mode (raw input)
    tty.c_lflag &= ~ECHO;            // Disable echo
    tty.c_lflag &= ~ECHOE;           // Disable erase
    tty.c_lflag &= ~ECHONL;          // Disable newline echo
    tty.c_lflag &= ~ISIG;            // Disable signal chars

    tty.c_iflag &= ~(IXON | IXOFF | IXANY); // Disable flow control
    tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL);

    tty.c_oflag &= ~OPOST;           // Raw output
    tty.c_oflag &= ~ONLCR;           // Disable newline -> CRLF translation

    tty.c_cc[VTIME] = 10;            // Wait up to 1.0 seconds (VTIME * 0.1 s)
    tty.c_cc[VMIN] = 0;              // Return as soon as any data is available

    if (tcsetattr(portFd, TCSANOW, &tty) != 0) {
        SPDLOG_ERROR("Error from tcsetattr: {}", strerror(errno));
        return -1;
    }

    return 0;
} 

int Serial::readSer(void* buffer, size_t nBytes) const{
    //! Attempt to read incoming bytes    
    int n = read(portFd, buffer, nBytes);
    if (n < 0) {
        SPDLOG_ERROR("Read error: {}", strerror(errno));
        close(portFd);
        return -1;
    }

    return n;
}

int Serial::readLineSer(std::string& line) const{
    //! A timeout will need to be implemented, but for now this is okay
    const uint8_t buffSize = 1; // Read character by character
    char buffer[buffSize]; // char buffer
    int n = 0; //! number of characters read
    while(true){
        n = readSer(static_cast<void*>(buffer), buffSize);
        if(n<0){
            // readSer() already closes the port for us if a read fails
            std::cerr << "Read line error: " << strerror(errno) << std::endl;
            return -1;
        }else if(n==0){
            // Skip empty lines
            continue;
        }

        char c = buffer[0];
        //! Only account for newlines
        if (c == '\n') {
            if (!line.empty()) {
                break;  // End of line received
            }
            // Ignore empty lines
        } else {
            line += c;
        }
    }

    // A line was succesfully read
    // if the return was non-zero
    return n;
}

int Serial::writeSer(const void* buffer, size_t nBytes) const{
    //! Attempt to write bytes to the serial port
    int n = write(portFd, buffer, nBytes);
    if (n < 0) {
        SPDLOG_ERROR("Write error: {}", strerror(errno));
        return -1;
    }

    return n;
}

int Serial::clearReadBuffer(){

    return tcflush(portFd,TCIFLUSH);
}