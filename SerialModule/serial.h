//! file: Contains the class and helper routins to configure the serial port
//! for reading

#include "../interfaces/serialInterface.h"
#include <fcntl.h>      // open
#include <unistd.h>     // read, close
#include <termios.h>    // termios, tcgetattr, tcsetattr
#include <cstring>      // strerror
#include <cstdint>
#include <cerrno>
#include <iostream>
#include <string>
#include <spdlog/spdlog.h>

#ifndef SERIALMODULE
#define SERIALMODULE

class Serial : public SerialInterface{
    public:
        //! brief: Class constructor, must have baud and port
        Serial();

        //! See the SerialInterface header file
        //! param[in] fd = Port Address
        //! param[in] baud = Define entry from termios that matches the desired baud rate
        int begin(const char* portName, speed_t baudRate = B19200) override;
        int end() override;
        int readSer(void* buffer, size_t nBytes) const override;
        int readLineSer(std::string& line) const override;
        int writeSer(const void* buffer, size_t nBytes) const override;
        int clearReadBuffer() override;

    private:
        const char* portName;
        int portFd;
        speed_t baudRate;

        //! brief: Configures the serial port to match
        //! the bahavior we want
       int configurePort(); 
};

#endif