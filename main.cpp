#include <iostream>
#include <termios.h>
#include "SerialModule/serial.h"
#include "CordDecoder/cordDecoder.h"
#include <vector>
#include <array>
#include <cmath>
#include <limits>
#include <iostream>
#include <spdlog/spdlog.h>
#include <toml++/toml.h>
#include <fstream>

#include "SettingsModule/settings.h"

//! Define flag for compiling in Windows using cygwin
//! DEBUG flag is defined in CMakeLists.txt
#define LINUX

#ifndef WINDOWS
#define LINUX
#endif

//! Serial port name of the receiving microcontroller
#ifdef LINUX
const char* port_name = "/dev/ttyACM0"; 
#else
const char* port_name = "/dev/ttyS2"; 
#endif
//! Total number of receivers in the system
const uint8_t numberOfReceivers = 6;

//! Array of the two KNOWN calibration distances for each receiver
//! Index 0 for a particular receiver is the receivers distance to 
//! calibration point 1, and index 1 is the distance to calibration 
//! point 2
//! Distances in cm
const std::vector<std::array<double, 2>> calDistances = {
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
static constexpr uint8_t calReads = 5;

//! Global Settings
SettingsModule settings;
std::string serialPort;
uint8_t numReceivers;
uint8_t numCalReads;

//! Serial interface object
Serial rxDev; // Create the receiving device (Arduino)
//! Instantiate decoder class
CordDecoder decoder(rxDev);

//! Initialization routine to setup various things
int init(){
    spdlog::set_level(spdlog::level::debug); 
    SPDLOG_INFO("Initializing UltraGPS Positioning System...");
    //! Instantiate serial device
    //! TODO Set a flag when SERIAL module is initiated
    if( rxDev.begin(port_name, B19200) < 0 ){
        SPDLOG_ERROR("Failed to open serial port {}", port_name);
        return -1;
    }
    SPDLOG_INFO("Serial port {} opened at 19200 baud", port_name);

    // Load the calibration data
    if (settings.getSerialPort(serialPort)) {
        SPDLOG_INFO("Using serial port from settings: {}", serialPort);
    } else {
        SPDLOG_WARN("Serial port not set");
    }

    if (settings.getNumberOfReceivers(numReceivers)) {
        decoder.begin(numReceivers, Unit::cm);
        SPDLOG_INFO("Num Receivers: {}", numReceivers);
    } else {
        SPDLOG_WARN("Number of receivers not set");
    }
    
    if (settings.getCalibrationReads(numCalReads)) {
        SPDLOG_INFO("Num Calibration Reads: {}", numCalReads);
    } else {
        SPDLOG_WARN("Calibration reads not set");
    }
    
    std::vector<std::array<double, 2>> loadedDistances = settings.getCalibrationDistances();
    if (loadedDistances.size() == numberOfReceivers) {
        SPDLOG_INFO("Loaded calibration distances:");
        for (size_t i = 0; i < loadedDistances.size(); ++i) {
            SPDLOG_INFO("Receiver {}: {}, {}", i, loadedDistances[i][0], loadedDistances[i][1]);
        }
    } else {
        SPDLOG_WARN("Calibration distances not set or incomplete");
    }

    if (settings.hasCalibrationData()) {
        SPDLOG_INFO("Loading calibration offsets...");
        for (size_t i = 0; i < numberOfReceivers; ++i) {
            double slope, intercept;
            if (settings.getReceiverOffset(i, slope, intercept)) {
                decoder.setOffset(i, slope, intercept);
                SPDLOG_INFO("Receiver {} Offset: Slope={}, Intercept={}", i, slope, intercept);
            } else {
                SPDLOG_WARN("Offset for receiver {} not set", i);
            }
        }
    } else {
        SPDLOG_WARN("No calibration data found. Please run calibration.");
    }

    return 1;
}

int main() {

    if(init() < 0){
        SPDLOG_ERROR("Failed to initialize positioning system...exiting");
        return -1;
    }else{
        SPDLOG_INFO("Initialization complete");
    }


    //exit(0);

    //! Run calibration routines for all receivers
    SPDLOG_INFO("Running calibration routine...");
    //decoder.calcAllReceiverOffsets(calReads, calDistances);

    while(true){
        decoder.calcDistances();
        decoder.printDistances();
        decoder.getPosition();
    }
    
}
