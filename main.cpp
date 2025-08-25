#include <iostream>
#include <termios.h>
#include "SerialModule/serial.h"
#include "CordDecoder/cordDecoder.h"


int main() {
    
    
    const char* port_name = "/dev/ttyACM0";  // Change to your serial port
    
    //! Instantiate serial device
    Serial rxDev(port_name,B19200); // Create the receiving device (Arduino)
    //! Instntiate decoder class
    CordDecoder decoder(rxDev, 2, Unit::cm);


    std::cout << "Starting program..." << std::endl;
    std::string line = "";
    
    //! TODO Set a flag when read module is initiated
    rxDev.begin();

    while(rxDev.readLineSer(line) > 0){
        std::cout << "Received line: " << line << std::endl;
        int cnt = 0;
        std::cout << "ITOA Num: " << (cnt = atoi(line.c_str())) << std::endl;
        int distance = 0.034375*cnt-63.875;
        std::cout << "distance (cm): " << distance << std::endl;
        std::cout << "decoder class distance: " << decoder.getDistance(0) << std::endl;
        line = "";
    }
    
}
