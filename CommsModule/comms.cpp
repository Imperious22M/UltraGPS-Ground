#include "comms.h"
#include <chrono>
#include <thread>
#include <algorithm>

std::string Comms::extractCommand(const std::string& rawCmd) const{
    std::string cmd = rawCmd;
    while(!cmd.empty() && (cmd.back() == '\n' || cmd.back() == '\r')){
        cmd.pop_back();
    }
    return cmd;
}

bool Comms::isValidCommand(const std::string& rawCmd) const{
    return getCommandType(rawCmd) != CommandType::Invalid;
}

Comms::CommandType Comms::getCommandType(const std::string& rawCmd) const{
    std::string cmd = extractCommand(rawCmd);
    auto it = validCommands.find(cmd);
    if(it != validCommands.end()){
        return it->second;
    }
    return CommandType::Invalid;
}

int Comms::begin(std::string portName, int baudRate, int timeout){

    this->serialPort = portName;
    this->baudRateInt = baudRate;
    this->baudRate = B38400;
    this->responseTimeout = timeout;

    switch (baudRate)
    {
    case 9600u:
        this->baudRate = B9600;
        break;
    case 19200u:
        this->baudRate = B19200;
        break;
    case 38400u:
        this->baudRate = B38400;
        break;
    case 57600u:
        this->baudRate = B57600;
        break;
    case 115200u:
        this->baudRate = B115200;
        break;
    default:
        SPDLOG_ERROR("Invalid baud rate specified: {}", baudRate);
        return -1;
    }
    //! Initialize serial device and start RX/TX threads
    if( serial.begin(this->serialPort.c_str(), this->baudRate) < 0 ){
        SPDLOG_ERROR("Failed to open serial port {}", serialPort);
        return -1;
    }
    SPDLOG_INFO("Serial port {} opened at {} baud", serialPort, baudRateInt);

    /*Clear startup messages */
    serial.clearReadBuffer(); 
    return 0;    
}

int Comms::end(){

    serial.end();
    return 0;
}

bool Comms::checkResponse(std::string &response, const std::string marker){
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(this->responseTimeout);
    std::string line;

    while(true){
        if(serial.getReceived(line)){
            if(line.rfind(marker, 0) == 0){ // Check if line contains marker
                response = line;
                return true;
            }
        }
        if(std::chrono::steady_clock::now() > deadline){
            SPDLOG_WARN("Timeout waiting for serial response");
            SPDLOG_WARN("Response received before timeout: {}", line);
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

/* Sends a normal command and waits for a normal response */
bool Comms::sendNormalCommand(std::string command, std::string &response){

    std::string line;

    /* If transitioning from continous mode, clear the read buffer*/
    if(this->continousMode){
        this->continousMode = false;
        serial.clearReadBuffer();
    }

    serial.sendData(command+"\n");
    SPDLOG_INFO("Sent {} command to serial device", command);
    if(!checkResponse(line, this->normalCommandMarker)){
        return false;
    }
    SPDLOG_INFO("Received from serial: {}", line);
    response = line;

    return true;
}

/* Sends the continous command  and waits until streaming serial messages are received 
* or times out
*/
bool Comms::sendContinousCommand(std::string command){

    std::string line;
    this->continousMode = true;

    serial.clearReadBuffer();
    serial.sendData(command+"\n");
    SPDLOG_INFO("Sent {} command to serial device", command);
    if(!checkResponse(line, this->continousCommandMarker)){
        return false;
    }
    SPDLOG_INFO("Received from serial: {}", line);

    return true;
}

bool Comms::getContinousResponse(std::string &response){

    std::string line;
    if(!checkResponse(line, this->continousCommandMarker)){
        return false;
    }
    SPDLOG_INFO("Received from serial: {}", line);
    response = line;
    
    return true;
}