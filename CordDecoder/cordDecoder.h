//! file: This module contains all the calibration paramaters and suibroutines to calculate the x and y position 
//! using the ultrasonic sensors

#include "../interfaces/serialInterface.h"
#include <iostream>
#include <vector>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>
#include <array>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <algorithm>
#include <chrono>
#include <spdlog/spdlog.h>

#ifndef CORDDECODERMODULE
#define CORDDECODERMODULE

//! Defines the units that the the cordinate decoder class is using
enum class Unit{
    cm,
    in
};

class CordDecoder{
    public:

        //! Struct to hold the x,y cordinates for various things
        struct Point { double x, y; };
        
        //! Constructor to initialiaze class from a given number of receivers
        //! with no previously known scaling factors
        //CordDecoder(SerialInterface &serial, uint8_t numberOfReceivers, Unit unit);
        CordDecoder(SerialInterface &serial);

        //! Initialization method, MUST be called before any other methods
        //! Returns 1 on success, negative value on failure
        int begin(uint8_t numberOfReceivers, Unit unit);

        //! Calculate all receivers using the histogram method
        //! and store the offsets in the scaling factors array
        //! Offsets is a 2d vector where the first index is the receiver
        //! and the second index is the A or B value (0 = A, 1 = B)
        //! Ex: offsets[n][0] = A value for receiver n
        //!     offsets[n][1] = B value for receiver n
        int calcAllReceiverOffsets(uint16_t numReads, const std::vector<std::array<double,2>>& knownDistances);

        //! Sets the anchor entries for the receiver positions
        //! Takes a vector of doubles representing the x and y positions
        //! of each receiver in the system
        int setAnchors(const std::vector<std::array<double, 2>>& anchors);

        //! set the offset of a certain receiver
        int setOffset(uint8_t receiver, double a, double b);

        //! Get the offset of a certain receiver
        int getOffset(uint8_t receiver, double &a, double &b);

        //! Returns the total number of receivers in the system
        uint8_t getReceiverCount();

        //! Calculate the distances from all receivers
        int calcDistances();

        //! Prints the last calculated receiver distances to the terminal
        void getDistances(std::vector<double> &distances);

        //! Get the current position
        void getPosition(Point &estimatedPosition);

        //! Function to get the most recent raw serial counts received
        //! Input: A string containing to store the raw serial data
        void getRecvCounts(std::string& data);

        //! Math functions for predicting the position

        //! Solve a 2x2 system of equations
        Point solve2x2(const double M[2][2], const double v[2], bool &ok);

        // returns an initial guess for (x,y)
        Point initialGuessFromRanges(std::vector<Point> &anchors,
                            std::vector<double> &ranges);

        // Compute residuals and Jacobian for given (x,y)
        void computeResidualsAndJacobian(
            Point &p,
            std::vector<Point> &anchors,
            std::vector<double> &ranges,
            std::vector<double> &residuals,
            std::vector<std::array<double,2>> &J);

        // Levenberg–Marquardt optimization
        Point levenbergMarquardt(
            std::vector<Point> &anchors,
            std::vector<double> &ranges,
            Point initialGuess,
            int maxIters = 100,
            double tau = 1e-3,
            double eps1 = 1e-8,
            double eps2 = 1e-8);

        //! Gets X number of valid counts from all receivers and returns the 
        //! status of the counts received
        int getHistogramCounts(uint8_t numReads, std::vector<uint32_t>& validCounts);
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
        //! Stores the most recent calculated distances
        std::vector<double> recvDistances;

        //! Receiver tower position in the grid, in cm
        //! NEED TO BE CHANGED TO MATCH THE GRID!
        //! First entry is R0, then R1, then R2...
        std::vector<CordDecoder::Point> anchors;

        //! Position variables
        CordDecoder::Point truePos {0, 0};
        std::vector<double> ranges;

        //! Store the which unit is being used
        Unit unit;
        //! Store the serial interface with the receiver's microcontroller
        SerialInterface &serial;
        //! Store the separator between count readings
        const char delimiter =  ',';

        //! Private helper function to check if a receiver index is out of bounds
        //! or is unable to be used (future functionality)
        bool isReceiverOk(uint8_t receiver);

        //! Function to help receive the serial output lines and store the counts
        //! in an private array buffer (recvCounts)
        //! returns the number of bytes that were received
        //! TODO: Add basic count validation to this function 
        //! (aka if number is too large throw it out and set that count to 0)
        int serialRecvCounts();




};

#endif