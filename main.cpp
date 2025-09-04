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
const uint8_t numberOfReceivers = 6;
//! Array of the two KNOWN calibration distances for each receiver
//! Index 0 for a particular receiver is the receivers distance to 
//! calibration point 0, and index 1 is the distance to calibration 
//! distance 2
//! Distances in cm
const double calDistances[numberOfReceivers][2] = {
    //! R1 (A0)
    {425,528},
    //! R2 (A1)
    {307,307+297},
    //! R3 (A2)
    {419,530},
    //! R4 (A3)
    {412,327},
    //! R5 (A4)
    {459,162},
    //! R6 (A5)
    {410,337}
};

//! Calibration parameters
static constexpr uint8_t calReads = 50;


int main() {
    
    //! Instantiate serial device
    Serial rxDev(port_name); // Create the receiving device (Arduino)
    //! TODO Set a flag when read module is initiated
    if( rxDev.begin() < 0 ){
        return -1;
    }

    //! Instantiate decoder class
    CordDecoder decoder(rxDev, numberOfReceivers, Unit::cm);
    //! Run calibration routines for all receivers
    for(int r=0;r<numberOfReceivers;r++){
        decoder.calcReceiverOffset(r,calReads,calDistances[r][0],calDistances[r][1]);
    }
    //! ALL decoders MUST have offsets in order to return a distance 
    //! other than 0. 
    //decoder.setOffset(0,0.0384,-73.4615);
    //decoder.setOffset(1,0.0341686,-55.8656);
    //decoder.setOffset(2,0.0451128,-88.1278);

    std::cout << "Starting program..." << std::endl;
    while(true){
        decoder.calcDistances();
        decoder.printDistances();
        decoder.getPosition();
    }
    
}
