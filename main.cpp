#include <iostream>
#include <termios.h>
#include "SerialModule/serial.h"
#include "CordDecoder/cordDecoder.h"


//! Serial port name of the receiving microcontroller
const char* port_name = "/dev/ttyACM0"; 
//! Total number of receivers in the system
const uint8_t numberOfReceivers = 3;
//! Array of two calibration distances for each receiver
const uint16_t calDistances[numberOfReceivers][2] = {0};

int main() {
    
    //! Instantiate serial device
    Serial rxDev(port_name,B19200); // Create the receiving device (Arduino)
    //! TODO Set a flag when read module is initiated
    if( rxDev.begin() < 0 ){
        return -1;
    }

    //! Instantiate decoder class
    CordDecoder decoder(rxDev, numberOfReceivers, Unit::cm);
    //decoder.calcReceiverOffset(0,50,12.0,30.0);
    
    decoder.setOffset(0,0.034375,-63.875);
    decoder.setOffset(1,0.034375,-63.875);
    decoder.setOffset(2,0.034375,-63.875);


    std::cout << "Starting program..." << std::endl;
    

    while(true){
        //std::cout << "Decoder class distance: " << decoder.getDistance(0) << " cm" << std::endl;
        decoder.calcDistances();
        decoder.printDistances();
    }
    
}
