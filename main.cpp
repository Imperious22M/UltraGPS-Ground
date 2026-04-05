#include <cstdint>
#include <iostream>
#include <termios.h>
#include <vector>
#include <array>
#include <cmath>
#include <iostream>
#include <spdlog/spdlog.h>
#include <toml++/toml.h>
#include <chrono>
#include <thread>
#include <sstream>
#include "ServerModule/serverModule.h"
#include "SerialModule/serial.h"

//! Serial Interface
Serial rxDev; // Create the receiving device (Arduino)
//! Set the default baud rate for the serial device
constexpr speed_t baudRate = B38400; // Default baud rate for the serial device
//! Default Serial Port
std::string serialPort = "/dev/ttyACM0"; 

//! UDP Server
UDPServer server;
constexpr unsigned int commandPort = 9000;
constexpr unsigned int distanceSendPort = 8002;
constexpr unsigned int serialSendPort = 8003;
constexpr unsigned int distanceClientPort = 9001;  // Python listens for distances here
constexpr unsigned int serialClientPort = 9002;    // Python listens for serial here


//! Initialization routine to setup various things
int initSystem(){
    spdlog::set_level(spdlog::level::debug); 
    SPDLOG_INFO("Initializing UltraGPS Positioning System...");

    //! Instantiate serial device
    if( rxDev.begin(serialPort.c_str(), baudRate) < 0 ){
        SPDLOG_ERROR("Failed to open serial port {}", serialPort);
        return -1;
    }
    SPDLOG_INFO("Serial port {} opened at default 38400 baud", serialPort);
    
    return 1;
}

int main() {

    server.addListenPort(commandPort);
    server.addSendPort(distanceSendPort, distanceClientPort);
    server.addSendPort(serialSendPort, serialClientPort);
    server.start();

    if(initSystem() < 0){
        SPDLOG_ERROR("Failed to initialize positioning system...exiting");
        return -1;
    }else{
        SPDLOG_INFO("Initialization complete");
        //! Wait for all terminal messages to print
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));  // 1000ms
    }

    while(true){
        //! Main positioning loop
        std::string serialData;
        std::stringstream distanceStream;

        //! Check for incoming UDP messages
        UDPMessage incomingMsg;
        std::string pulseCmd = "P\n";
        while(server.getIncomingMessage(incomingMsg)) {
            SPDLOG_INFO("Received UDP message on port {} from {}: {}", 
                       incomingMsg.port, incomingMsg.sender_ip, incomingMsg.message);

            //! Send the distances and serial data to connected client
            server.sendMessage(distanceSendPort, "0\n");
            server.sendMessage(serialSendPort, "O\n");
        }

        //! Sleep for a short duration to prevent busy-waiting
        std::this_thread::sleep_for(std::chrono::milliseconds(1));  //
    }
}
