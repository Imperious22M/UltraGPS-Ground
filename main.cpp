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

//! Network server (TCP commands + UDP continuous stream)
ServerModule server;
unsigned int clientTimeoutMs; // Timeout for client communications (reserved for future use)
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

    //! Construct modules before banner so IP address is resolved for display
    Comms comms;
    ServerModule server(tcpPort, udpPort, netDevice);

    //! Print ASCII art banner — temporarily switch to bare format (no level prefix)
    spdlog::set_pattern("%v");
    spdlog::info("");
    spdlog::info(" _    _ _ _              _____ _____   _____ ");
    spdlog::info("| |  | | | |            / ____|  __ \\ / ____|");
    spdlog::info("| |  | | | |_ _ __ __ _| |  __| |__) | (___  ");
    spdlog::info("| |  | | | __| '__/ _` | | |_ |  ___/ \\___ \\ ");
    spdlog::info("| |__| | | |_| | | (_| | |__| | |     ____) |");
    spdlog::info(" \\____/|_|\\__|_|  \\__,_|\\_____|_|    |_____/ ");
    spdlog::info("               Ground Station  v1.0");
    spdlog::info("");
    spdlog::info("  +-------------------------------------------------+");
    spdlog::info("  |  Serial Port  : {:<32}|", serialPort);
    spdlog::info("  |  Baud Rate    : {:<32}|", std::to_string(baudRateInt) + " bps");
    spdlog::info("  |  IP Address   : {:<32}|", server.getIP());
    spdlog::info("  |  TCP Port     : {:<32}|", tcpPort);
    spdlog::info("  |  UDP Port     : {:<32}|", udpPort);
    spdlog::info("  |  Net Device   : {:<32}|", netDevice.empty() ? "auto-detect" : netDevice);
    spdlog::info("  +-------------------------------------------------+");
    spdlog::info("");
    spdlog::set_pattern("[%^%l%$] %v");

    //! Initialize Comms module (serial link to base-station Arduino)
    spdlog::info("Initializing Comms module...");
    if(comms.begin(serialPort, baudRateInt) < 0){
        spdlog::error("Failed to initialize Comms module");
        return -1;
    }
    spdlog::info("Comms module initialized  [{} @ {} bps]", serialPort, baudRateInt);

    std::this_thread::sleep_for(std::chrono::seconds(7)); // Wait for serial device to initialize
    std::string res;
    comms.sendNormalCommand("P", res);

    //! Start Server module (TCP command socket + UDP continuous stream)
    spdlog::info("Starting Server module...");
    if(!server.start()){
        spdlog::error("Failed to start Server module");
        return -1;
    }
    spdlog::info("Server module started  [TCP:{} | UDP:{} | IP:{}]", tcpPort, udpPort, server.getIP());

    while(true){
        std::string cmd;

        if(server.receiveCommand(cmd)){
            SPDLOG_INFO("Received TCP command: {}", cmd);

            if(!comms.isValidCommand(cmd)){
                SPDLOG_WARN("Discarding invalid command: {}", comms.extractCommand(cmd));
            } else {
                std::string cleanCmd = comms.extractCommand(cmd);
                Comms::CommandType type = comms.getCommandType(cmd);

                if(type == Comms::CommandType::Normal){
                    std::string res;
                    if(!comms.sendNormalCommand(cleanCmd, res)){
                        SPDLOG_ERROR("Failed to send normal command through comms module");
                    }
                    if(!res.empty()){
                        SPDLOG_INFO("Received response for normal command: {}", res);
                        if(!server.sendCommand(res)){
                            SPDLOG_ERROR("Failed to send response through server module");
                        } else {
                            SPDLOG_INFO("Sent response through server module");
                        }
                    } else {
                        SPDLOG_ERROR("No response received for normal command");
                    }
                } else if(type == Comms::CommandType::Continous){
                    if(!comms.sendContinousCommand(cleanCmd)){
                        SPDLOG_ERROR("Failed to send continuous command through comms module");
                    } else {
                        SPDLOG_INFO("Sent continuous command through comms module");
                    }
                }
            }
        }

        if(comms.isContinousMode()){
            std::string res;
            if(comms.getContinousResponse(res)){
                SPDLOG_INFO("Received SERIAL continuous data: {}", res);
                if(!server.sendContinuous(res)){
                    SPDLOG_ERROR("Failed to send continuous data through server module");
                } else {
                    SPDLOG_INFO("Sent continuous data through server module");
                }
            } else {
                SPDLOG_ERROR("Failed to get continuous response from comms module");
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }


}
