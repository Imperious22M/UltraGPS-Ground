#include <cstdint>
#include <iostream>
#include <termios.h>
#include "SerialModule/serial.h"
#include "CordDecoder/cordDecoder.h"
#include <vector>
#include <array>
#include <cmath>
#include <iostream>
#include <spdlog/spdlog.h>
#include <toml++/toml.h>
#include <chrono>
#include <thread>
#include <sstream>
#include "SettingsModule/settings.h"
#include "ServerModule/serverModule.h"

//! Define flag for compiling in Windows using cygwin
//! DEBUG flag is defined in CMakeLists.txt
#define LINUX

#ifndef WINDOWS
#define LINUX
#endif

//! Serial port name of the receiving microcontroller
#ifdef LINUX
//! PLACEHOLDER for linux select
#else
//! PLACEHOLDER for windows select
#endif

SettingsModule settings;
//! Array of known calibration distances for each receiver
std::vector<std::array<double, 2>> calDistances;
//! Calibration parameters
std::string serialPort;
std::string units;
uint8_t numReceivers;
uint8_t numCalReads;
uint8_t calState;

//! Serial interface object
Serial rxDev; // Create the receiving device (Arduino)
//! Instantiate decoder class
CordDecoder decoder(rxDev);

//! Initialization routine to setup various things
int initSystem(){
    spdlog::set_level(spdlog::level::debug); 
    SPDLOG_INFO("Initializing UltraGPS Positioning System...");

    //! Instantiate serial device
    //! TODO Set a flag when SERIAL module is initiated
    settings.getSerialPort(serialPort);
    if( rxDev.begin(serialPort.c_str(), B38400) < 0 ){
        SPDLOG_ERROR("Failed to open serial port {}", serialPort);
        return -1;
    }
    SPDLOG_INFO("Serial port {} opened at 38400 baud", serialPort);

    // Get the units used
    settings.getUnits(units);
    if(units.compare("cm")==0){
        SPDLOG_INFO("Units set to cm");
    } else if(units.compare("in")==0){
        SPDLOG_INFO("Units set to in");
    } else {
        SPDLOG_WARN("Units not set, defaulting to cm");
        units = "cm";
    }

    //! Initialize decoder based on number of receivers
    if (settings.getNumberOfReceivers(numReceivers)) {
        if(units.compare("cm")==0){
            decoder.begin(numReceivers, Unit::cm);
        } else {
            decoder.begin(numReceivers, Unit::in);
        }
        SPDLOG_INFO("Num Receivers: {}", numReceivers);
    } else {
        SPDLOG_WARN("Number of receivers not set");
    }
    
    //! Get the number of calibration reads
    if (settings.getCalibrationReads(numCalReads)) {
        SPDLOG_INFO("Num Calibration Reads: {}", numCalReads);
    } else {
        SPDLOG_WARN("Calibration reads not set");
    }
    
   //! Initialize calibration distances
   for(uint8_t i = 0; i < numReceivers; ++i){
        std::array<double, 2> distances = settings.getReceiverCalibrationDistances(i);
        calDistances.push_back(distances);
       SPDLOG_INFO("Receiver {}: Distances: {}, {}", i, distances[0], distances[1]);
   }

   //! Set the anchor points (location of towers)
   std::vector<std::array<double, 2>> anchorPoints;
   for(uint8_t i = 0; i < numReceivers; ++i){
        std::array<double, 2> position = settings.getReceiverPosition(i);
        anchorPoints.push_back(position);
        SPDLOG_INFO("Receiver {}: Position: {}, {}", i, position[0], position[1]);
   }
   decoder.setAnchors(anchorPoints);

   //! If the system has already been calibrated, initialize the offset array
   settings.getCalState(calState);
    if(calState == 1){
        SPDLOG_INFO("System already calibrated, loading offsets...");
        for(uint8_t i = 0; i < numReceivers; ++i){
            std::array<double, 2> offsets = settings.getReceiverOffsets(i);
            decoder.setOffset(i, offsets[0], offsets[1]);
            SPDLOG_INFO("Receiver {}: Offsets: {}, {}", i, offsets[0], offsets[1]);
        }
    } else {
        SPDLOG_WARN("System not calibrated, please run calibration routine");
    }
    
    return 1;
}

int main() {


    //! UDP Server Broadcasts
    UDPServer server;
    const uint broadcastInterval = 1000; // millisecond delay broadcast interval
    server.setBroadcastInterval(broadcastInterval);
    constexpr unsigned int commandPort = 8000;
    constexpr unsigned int distanceBroadcastPort = 8002;
    constexpr unsigned int serialBroadcastPort = 8003;

    server.addReceiveOnlyPort(commandPort);
    server.addBroadcastPort(distanceBroadcastPort,"",0); 
    server.addBroadcastPort(serialBroadcastPort,"",0); 
    server.start();

    
    if(initSystem() < 0){
        SPDLOG_ERROR("Failed to initialize positioning system...exiting");
        return -1;
    }else{
        SPDLOG_INFO("Initialization complete");
        //! Wait for all terminal messages to print
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));  // 1000ms
    }

    //! Run calibration routines for all receivers if system is not calibrated
    if(calState == 0){
        if(numCalReads == 0 || calDistances.size() != numReceivers){
            SPDLOG_ERROR("Calibration parameters not set correctly, cannot run calibration");
            return -1;
        }

        SPDLOG_INFO("Running calibration routine...");
        decoder.calcAllReceiverOffsets(numCalReads, calDistances);
        SPDLOG_INFO("Calibration complete, saving offsets...");
        //! Save calibration offsets to settings file
        double a, b;
        for(uint8_t i = 0; i < decoder.getReceiverCount(); ++i){
            decoder.getOffset(i, a, b);
            std::array<double, 2> offsets = {a, b};
            settings.setReceiverOffset(i, offsets);
            SPDLOG_INFO("Receiver {}: Offsets: {}, {}", i, offsets[0], offsets[1]);
        }
        settings.setCalState(1); // Set system as calibrated
    }

    while(true){
        //! Main positioning loop
        std::string serialData;
        std::stringstream distanceStream;
        CordDecoder::Point pos;
        std::vector<double> distances;

        //! Check for incoming UDP messages
        UDPMessage incomingMsg;
        std::string pulseCmd = "P\n";
        while(server.getIncomingMessage(incomingMsg)) {
            SPDLOG_INFO("Received UDP message on port {} from {}: {}", 
                       incomingMsg.port, incomingMsg.sender_ip, incomingMsg.message);

            //! Process incoming messages
            if(incomingMsg.message.compare(pulseCmd) == 0){
               //! Send the calculated distances to distance channel
               decoder.calcDistances();
               decoder.getRecvCounts(serialData);
               decoder.getDistances(distances);
               std::cout << "Serial Data: " << serialData << std::endl;
               std::cout << "Distances: ";
        
               for(size_t i = 0; i < distances.size(); ++i){
                   std::cout << distances[i] << " ";
                   distanceStream << distances[i] << " ";
               }
               distanceStream << "\n";
               std::cout << std::endl;
               
               //! Broadcast the distances and serial data
               server.sendMessage(distanceBroadcastPort, distanceStream.str());
               server.sendMessage(serialBroadcastPort, serialData);
            }
        }


    }
}
