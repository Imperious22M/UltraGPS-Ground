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

#ifndef SERIALMODULE
#define SERIALMODULE

class Serial : public SerialInterface{
    public:
        //! brief: Class constructor, must have baud and port
        //! specified at creation
        //! param[in] fd = Port Address
        //! param[in] baud = Define entry from termios that matches the desired baud rate
        Serial(const char* fd, speed_t baud = B19200);

        //! See the SerialInterface header file
        int begin() override;
        int end() override;
        int readSer(void* buffer, size_t nBytes) const override;
        int readLineSer(std::string& line) const override;
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