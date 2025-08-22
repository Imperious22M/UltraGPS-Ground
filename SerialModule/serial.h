//! file: Contains the class and helper routins to configure the serial port
//! for reading

#include <fcntl.h>      // open
#include <unistd.h>     // read, close
#include <termios.h>    // termios, tcgetattr, tcsetattr
#include <cstring>      // strerror
#include <cerrno>
#include <iostream>
#include <string>

class Serial{
    public:
        //! brief: Class constructor, must have baud and port
        //! specified at creation
        //! param[in] fd = Port Address
        //! param[in] baud = Define entry from termios that matches the desired baud rate
        Serial(const char* fd, speed_t baud);

        //! brief: Attempts to open and configure the port
        int begin();
        //! brief: Close the serial port and perform any cleanup
        int end();

       //! Read N-Bytes from the serial port
       int readSer(void* buffer, size_t nBytes);
       //! Read one newline-terminated line from the serial port
       int readLineSer(std::string& line);


    private:
        const char* portName;
        int portFd;
        speed_t baudRate;

        //! brief: Configures the serial port to match
        //! the bahavior we want
       int configurePort(); 

};