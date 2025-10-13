//! file: contains the interface definition for the serial class to allow other classes
//! to more easily use the functions available by the serial module

#include <string>
#include <termios.h>

#ifndef SERIALMODULEINTERFACE
#define SERIALMODULEINTERFACE

class SerialInterface{
    public:

        //! brief: Attempts to open and configure the port
        virtual int begin(const char* portName, speed_t baudRate) = 0;
        //! brief: Close the serial port and perform any cleanup
        virtual int end() = 0;

        //! Read N-Bytes from the serial port
        virtual int readSer(void* buffer, size_t nBytes) const = 0;
        //! Read one newline-terminated line from the serial port
        virtual int readLineSer(std::string& line) const = 0;
        //! Clear the reading buffer
        virtual int clearReadBuffer() = 0;
};

#endif