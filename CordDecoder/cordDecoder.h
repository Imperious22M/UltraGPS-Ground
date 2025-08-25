//! file: This module contains all the calibration paramaters and suibroutines to calculate the x and y position 
//! using the ultrasonic sensors

#include "../interfaces/serialInterface.h"
#include <iostream>
#include <vector>
#include <cstdint>

#ifndef CORDDECODERMODULE
#define CORDDECODERMODULE

//! Defines the units that the the cordinate decoder class is using
enum class Unit{
    cm,
    in
};

class CordDecoder{
    public:
        //! Constructor to initialiaze class from a given number of receivers
        //! with no previously known scaling factors
        CordDecoder(SerialInterface &serial, uint8_t numberOfReceivers, Unit unit);

        //! set the offset of a certain receiver
        int setOffset(uint8_t receiver, double a, double b);

        //! Get the distance from a certain receiver
        double getDistance(uint8_t receiver);

        //! Returns the total number of receivers in the system
        uint8_t getReceiverCount();

        //! Find the offsets of a certain receiver using a two known distance
        //! and store them in the scaling factiors array
        //! Takes the receiver index and the number of reads to take
        //! for calibration
        //! TODO
        int calcReceiverOffset(uint8_t receiver, uint16_t numReads, double knownDistance1, double knownDistance2);

    private:
        //! Struct to hold the A and B values of the 
        //! m=Ax+B equation.
        //! m = distance from receiving tower, A = scaling factor from ticks to distance
        //! and B = offset for ticks starting from non-zero (which is almost always the case)
        struct Factor{
            double a;
            double b;
        };
        //! Array to hold all of the scaling factor for all the receivers
        //! starting from the first value (A0) to the last value (An) 
        //! Receivers are indexed as an array (0 = the first receiver)
        //! and ( (max number of receivers)-1 = the last receiver)
        std::vector<Factor> scaleFactors;
        //! Stores the counts that are read from the serial line
        std::vector<uint32_t> recvCounts;
        //! Store the which unit is being used
        Unit unit;
        //! Store the serial interface with the receiver's microcontroller
        SerialInterface &serial;
        //! Store the most recent calculated distance
        uint32_t prevDistance;

        //! Private helper function to check if a receiver index is out of bounds
        //! or is unable to be used (future functionality)
        bool isReceiverOk(uint8_t receiver);

        //! Function to help receive the serial output lines and store the counts
        //! returns the number of bytes that were received
        int serialRecvCounts();

};

#endif