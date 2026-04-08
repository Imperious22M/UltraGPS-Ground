#include <cstdint>
#include <iostream>
#include <termios.h>
#include <vector>
#include <array>
#include <cmath>
#include <iostream>
#include <chrono>
#include <thread>
#include <sstream>
#define  SPDLOG_NO_DATE_TIME
#include <spdlog/spdlog.h>
#include <toml++/toml.h>
#include <argparse/argparse.hpp>
#include "ServerModule/serverModule.h"
#include "SerialModule/serial.h"
#include "CommsModule/comms.h"

//! Serial Interface with Arduino
Serial serial; 
speed_t baudRate; 
uint32_t baudRateInt;
std::string serialPort; 

//! Network device to bind to (empty string means default active network)
std::string netDevice = "";

//! UDP Server
UDPServer server;
constexpr unsigned int commandPort = 9000;
constexpr unsigned int distanceSendPort = 8002;
constexpr unsigned int serialSendPort = 8003;
constexpr unsigned int distanceClientPort = 9001;  // Python listens for distances here
constexpr unsigned int serialClientPort = 9002;    // Python listens for serial here
unsigned int clientTimeoutMs; // Timeout for client communications
unsigned int tcpPort; // TCP port for communication server 
unsigned int udpPort; // UDP port for communication server 

//! Argument parser
argparse::ArgumentParser program("UltraGPS Positioning System", "1.0");

//! Helper function that sets up the command-line arguments using argparse. 
void parseBuilder(){
    //! Add command-line arguments
    program.add_argument("-v", "--verbose")
        .help("Enable verbose logging")
        .flag();

    program.add_argument("-s", "--serial-port")
        .help("Serial port to connect to (default: /dev/ttyACM0)")
        .default_value(std::string("/dev/ttyACM0"));

    program.add_argument("-b", "--baud-rate")
        .help("Baud rate for the serial device (default: 38400)")
        .choices(9600u, 19200u, 38400u, 57600u, 115200u)
        .default_value(38400u)
        .scan<'u', unsigned int>();

    program.add_argument("-n", "--net-device")
        .help("Network device to connect that will be used for server communication (default: whatever network is active)")
        .default_value(std::string(""));

    program.add_argument("-t", "--tcp-port")
        .help("TCP port for communication server (default: 9000)")
        .default_value(9000u)
        .scan<'u', unsigned int>();

    program.add_argument("-u", "--udp-port")
        .help("UDP port for communcation server (default: 9001)")
        .default_value(9001u)
        .scan<'u', unsigned int>();

    program.add_argument("-T", "--server-timeout")
        .help("Server timeout in milliseconds (default: 5000)")
        .default_value(5000u)
        .scan<'u', unsigned int>();

}

int main(int argc, char *argv[]) {

    //! Build the argument parser
    parseBuilder(); 
    //! Decode arguments
    try {
        program.parse_args(argc, argv);
    } catch (const std::runtime_error& err) {
        std::cerr << err.what() << std::endl;
        return 1;
    }

    //! Populate variables from arguments
    serialPort = program.get<std::string>("--serial-port");
    netDevice = program.get<std::string>("--net-device");
    tcpPort = program.get<unsigned int>("--tcp-port");
    udpPort = program.get<unsigned int>("--udp-port");
    clientTimeoutMs = program.get<unsigned int>("--server-timeout");
    switch (program.get<unsigned int>("--baud-rate"))
    {
    case 9600u:
        baudRate = B9600;
        baudRateInt = 9600;
        break;
    case 19200u:
        baudRate = B19200;
        baudRateInt = 19200;
        break;
    case 38400u:
        baudRate = B38400;
        baudRateInt = 38400;
        break;
    case 57600u:
        baudRate = B57600;
        baudRateInt = 57600;
        break;
    case 115200u:
        baudRate = B115200;
        baudRateInt = 115200;
        break;
    }

    //! Setup logging with spdlog
    spdlog::set_pattern("[%^%l%$] %v");
    if(program["--verbose"] == true){
        spdlog::set_level(spdlog::level::debug); 
    }

    SPDLOG_INFO("~~~~~~~~UltraGPS Positioning System~~~~~~~~");

    //! Initialize serial device and start RX/TX threads
    if( serial.begin(serialPort.c_str(), baudRate) < 0 ){
        SPDLOG_ERROR("Failed to open serial port {}", serialPort);
        return -1;
    }
    SPDLOG_INFO("Serial port {} opened at {} baud", serialPort, baudRateInt);

   std::this_thread::sleep_for(std::chrono::seconds(5)); // Wait for serial device to initialize 

    //! Test the comms module
    serial.end(); // Stop serial threads to prevent interference with comms module testing
    SPDLOG_INFO("Testing comms module...");
    Comms comms;
    if(comms.begin(serialPort, baudRateInt) < 0){
        SPDLOG_ERROR("Failed to initialize comms module");
        return -1;
    }
    std::this_thread::sleep_for(std::chrono::seconds(2)); // Wait for serial threads to cleanly exit
    std::string res;
    comms.sendNormalCommand("P", res);
    comms.sendNormalCommand("P", res);
    comms.sendNormalCommand("P", res);
    comms.sendNormalCommand("S", res);
    comms.sendContinousCommand("C");
    comms.getContinousResponse(res);
    comms.getContinousResponse(res);
    comms.getContinousResponse(res);
    comms.getContinousResponse(res);
    comms.sendNormalCommand("P", res);
    comms.sendNormalCommand("P", res);
    comms.sendNormalCommand("S", res);

    ////! Create and start the UDP/TCP server
    //server.addListenPort(commandPort);
    //server.addSendPort(distanceSendPort, distanceClientPort);
    //server.addSendPort(serialSendPort, serialClientPort);
    //server.start();

    //while(true){
        ////! Main positioning loop
        //std::string serialData;
        //std::stringstream distanceStream;

        ////! Check for incoming UDP messages
        //UDPMessage incomingMsg;
        //std::string pulseCmd = "P\n";
        //while(server.getIncomingMessage(incomingMsg)) {
            //SPDLOG_INFO("Received UDP message on port {} from {}: {}", 
                       //incomingMsg.port, incomingMsg.sender_ip, incomingMsg.message);

            ////! Send the distances and serial data to connected client
            //server.sendMessage(distanceSendPort, "0\n");
            //server.sendMessage(serialSendPort, "O\n");
        //}

        ////! Sleep for a short duration to prevent busy-waiting
        //std::this_thread::sleep_for(std::chrono::milliseconds(1));  //
    //}
}
