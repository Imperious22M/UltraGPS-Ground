#include <iostream>
#include <termios.h>
#include "SerialModule/serial.h"
#include "CordDecoder/cordDecoder.h"


int main() {
    
    
    const char* port_name = "/dev/ttyACM0";  // Change to your serial port
    
    //! Instantiate serial device
    Serial rxDev(port_name,B19200); // Create the receiving device (Arduino)
    //! TODO Set a flag when read module is initiated
    rxDev.begin();

    //! Instantiate decoder class
    CordDecoder decoder(rxDev, 1, Unit::in);
    decoder.calcReceiverOffset(0,10,13.25, 12);
    
    //decoder.setOffset(0,0.034375,-63.875);


    std::cout << "Starting program..." << std::endl;
    

    while(true){
        std::cout << "Decoder class distance: " << decoder.getDistance(0) << "in" << std::endl;
    }
    
}
