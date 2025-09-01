#include <iostream>
#include <termios.h>
#include "SerialModule/serial.h"
#include "CordDecoder/cordDecoder.h"
#include <vector>
#include <array>
#include <cmath>
#include <limits>
#include <iostream>

//! Serial port name of the receiving microcontroller
const char* port_name = "/dev/ttyACM0"; 
//! Total number of receivers in the system
const uint8_t numberOfReceivers = 3;
//! Array of two calibration distances for each receiver
const double calDistances[numberOfReceivers][2] = {
    //! R1
    {45,60},
    //! R3
    {45,60},
    //! R5
    {45,33}
};


int main() {
    
    //! Instantiate serial device
    Serial rxDev(port_name); // Create the receiving device (Arduino)
    //! TODO Set a flag when read module is initiated
    if( rxDev.begin() < 0 ){
        return -1;
    }

    //! Instantiate decoder class
    CordDecoder decoder(rxDev, numberOfReceivers, Unit::cm);
    //! Used for initial calibration
    //R1
    //decoder.calcReceiverOffset(0,50,calDistances[0][0],calDistances[0][1]);
    //R2
    //decoder.calcReceiverOffset(1,50,calDistances[1][0],calDistances[1][1]);
    //R3
    //decoder.calcReceiverOffset(2,50,calDistances[2][0],calDistances[2][1]);
    decoder.setOffset(0,0.0384,-73.4615);
    decoder.setOffset(1,0.0341686,-55.8656);
    decoder.setOffset(2,0.0451128,-88.1278);

    std::cout << "Starting program..." << std::endl;
    while(true){
        decoder.calcDistances();
        decoder.printDistances();
        decoder.getPosition();
    }
    
}
