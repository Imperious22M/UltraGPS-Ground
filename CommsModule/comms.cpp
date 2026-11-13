//! @file comms.cpp
//! @brief Comms implementation: command validation, baud-rate handling, reply
//!        matching and the 'D' delay search.
//!
//! The API is documented in comms.h.

#include "comms.h"
#include <spdlog/spdlog.h>
#include <termios.h>
#include <chrono>
#include <thread>
#include <algorithm>
#include <cstdio>

namespace {
//! @brief termios speed constant for a host-supported baud rate.
//!
//! Defines the rates Comms can open: begin(), setBaudRate() and
//! hostSupportsBaud() (and through it reopen()) all use it.
//! @param baud Baud rate in bit/s.
//! @param out  Receives the matching `B<rate>` constant; unchanged for any
//!             other rate.
//! @return true for 9600, 19200, 38400, 57600 and 115200, false for any other
//!         rate.
bool toSpeed(long baud, speed_t& out){
    switch(baud){
    case 9600:   out = B9600;   return true;
    case 19200:  out = B19200;  return true;
    case 38400:  out = B38400;  return true;
    case 57600:  out = B57600;  return true;
    case 115200: out = B115200; return true;
    default:     return false;
    }
}
} // namespace

bool Comms::hostSupportsBaud(long baudRate){
    speed_t unused;
    return toSpeed(baudRate, unused);
}

std::string Comms::extractCommand(const std::string& rawCmd) const{
    std::string cmd = rawCmd;
    //! The firmware decodes on the first letter and ignores trailing blanks
    while(!cmd.empty() && (cmd.back() == '\n' || cmd.back() == '\r' ||
                           cmd.back() == ' '  || cmd.back() == '\t')){
        cmd.pop_back();
    }
    return cmd;
}

bool Comms::isValidCommand(const std::string& rawCmd) const{
    return getCommandType(rawCmd) != CommandType::Invalid;
}

Comms::CommandType Comms::getCommandType(const std::string& rawCmd) const{
    std::string cmd = extractCommand(rawCmd);
    if(cmd.empty()) return CommandType::Invalid;
    //! Argument commands: "H <int>" or "H<int>" (the firmware decoder skips
    //! spaces/tabs before an argument and parses it with strtol, so a sign is
    //! allowed). An out-of-range value is not an error on the firmware side: it
    //! changes nothing and the reply reports the value still in force.
    std::string letter = cmd.substr(0, 1);
    if(intArgCommands.count(letter)){
        size_t p = 1;
        while(p < cmd.size() && (cmd[p] == ' ' || cmd[p] == '\t')) ++p;
        if(p < cmd.size() && (cmd[p] == '-' || cmd[p] == '+')) ++p;
        size_t digits = p;
        while(p < cmd.size() && cmd[p] >= '0' && cmd[p] <= '9') ++p;
        size_t nDigits = p - digits;
        while(p < cmd.size() && (cmd[p] == ' ' || cmd[p] == '\t')) ++p;
        //! 1 to 9 digits (so the value fits in 32 bits), then only blanks
        bool isInt = nDigits > 0 && nDigits <= 9 && p == cmd.size();
        if(!isInt) return CommandType::Invalid;
        //! The argument is well formed: look up the letter alone
        cmd = letter;
    }
    //! Commands without an argument must be the letter alone ("P", not "P 1")
    auto it = validCommands.find(cmd);
    if(it != validCommands.end()){
        return it->second;
    }
    return CommandType::Invalid;
}

int Comms::begin(std::string portName, int baudRate, int timeout){

    this->serialPort = portName;
    this->baudRateInt = baudRate;
    this->responseTimeout = timeout;

    speed_t speed;
    if(!toSpeed(baudRate, speed)){
        SPDLOG_ERROR("Invalid baud rate specified: {}", baudRate);
        return -1;
    }
    //! Initialize serial device and start RX/TX threads
    if( serial.begin(this->serialPort.c_str(), speed) < 0 ){
        SPDLOG_ERROR("Failed to open serial port {}", serialPort);
        return -1;
    }
    SPDLOG_INFO("Serial port {} opened at {} baud", serialPort, baudRateInt);

    //! Drop bytes pending at open and the previous session's stored line (the
    //! board's boot output arrives later, during the settle time)
    serial.clearReadBuffer();

    return 0;
}

bool Comms::querySettings(){
    //! The trigger rate and sampling time live in the board's EEPROM, so learn
    //! them instead of assuming the defaults; the reply reaches the TUI through
    //! the RX tap. "H 0" is the fallback for firmware without 'F'.
    std::string reply;
    if(!sendNormalCommand("F 0", reply) && !sendNormalCommand("H 0", reply)){
        SPDLOG_WARN("Could not query trigger rate/sampling time, assuming {} Hz / {} ms",
                    triggerRateHz, samplingTimeMs);
        return false;
    }
    return true;
}

void Comms::end(){
    serial.end();
}

unsigned Comms::effectiveTimeout() const{
    //! A P/S reply needs up to one trigger period plus the sampling time
    return std::max<unsigned>(this->responseTimeout,
                              triggerPeriodMs() + this->samplingTimeMs + 200u);
}

void Comms::noteReply(const std::string& line){
    long ms = 0, hz = 0, top = 0;
    if(line.rfind("H:", 0) == 0 && std::sscanf(line.c_str() + 2, "%ld", &ms) == 1 &&
       ms >= 1 && ms <= 1000){
        this->samplingTimeMs = static_cast<unsigned>(ms);
    } else if(line.rfind("F:", 0) == 0 &&
              std::sscanf(line.c_str() + 2, " %ld , %ld , %ld", &hz, &top, &ms) == 3 &&
              hz >= 1 && hz <= 50){
        //! A new rate sets the sampling time to its maximum (80 % of the new
        //! period); the reply reports the sampling time in force either way
        this->triggerRateHz = static_cast<unsigned>(hz);
        if(ms >= 1 && ms <= 1000) this->samplingTimeMs = static_cast<unsigned>(ms);
    } else if(line.rfind("M: OK", 0) == 0){
        //! 'M' restored the firmware defaults
        this->samplingTimeMs = defaultSamplingTimeMs;
        this->triggerRateHz = defaultTriggerRateHz;
    }
}

bool Comms::checkResponse(std::string &response, const std::string marker){
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(effectiveTimeout());
    std::string line;

    while(true){
        if(serial.getReceived(line)){
            if(line.rfind(marker, 0) == 0){ // the line starts with the marker
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

bool Comms::sendNormalCommand(std::string command, std::string &response){

    std::string line;

    //! Always drop stale lines (continuous stream, or a reply that arrived after
    //! an earlier timeout) so this command does not pick up an old reply
    this->continousMode = false;
    serial.clearReadBuffer();

    serial.sendData(command+"\n");
    SPDLOG_INFO("Sent {} command to serial device", command);
    //! Wait for the marker of the command's letter ("N" for a letter not listed)
    auto marker = responseMarker.find(command.substr(0, 1));
    if(!checkResponse(line, (marker != responseMarker.end() ? marker->second : this->normalCommandMarker) + ":")){
        return false;
    }
    SPDLOG_INFO("Received from serial: {}", line);
    response = line;
    //! Keep the trigger rate / sampling time (and so the timeouts) in step
    noteReply(line);

    return true;
}

bool Comms::sendContinousCommand(std::string command){

    std::string line;
    //! Set before sending: the stream is expected from now on, even if the wait
    //! below times out
    this->continousMode = true;

    serial.clearReadBuffer();
    serial.sendData(command+"\n");
    SPDLOG_INFO("Sent {} command to serial device", command);
    //! The first stream line only confirms the start; it is not returned
    if(!checkResponse(line, this->continousCommandMarker + ":")){
        return false;
    }
    SPDLOG_INFO("Received from serial: {}", line);

    return true;
}

bool Comms::getContinousResponse(std::string &response){

    std::string line;
    if(!checkResponse(line, this->continousCommandMarker + ":")){
        return false;
    }
    SPDLOG_INFO("Received from serial: {}", line);
    response = line;

    return true;
}

int Comms::reopen(const std::string& portName, int baudRate){
    //! Check the rate before closing: a bad rate must not drop a working link
    //! (and the rollback reopen would reset the board through DTR)
    if(!hostSupportsBaud(baudRate)){
        SPDLOG_ERROR("Invalid baud rate specified: {}", baudRate);
        return -1;
    }
    const std::string oldPort = this->serialPort;
    const int oldBaud = this->baudRateInt;
    serial.end();
    this->continousMode = false;
    if(begin(portName, baudRate, static_cast<int>(this->responseTimeout)) == 0) return 0;
    //! Roll back so a typo does not drop a working link (begin() records
    //! oldPort/oldBaud even if this open fails too)
    SPDLOG_WARN("Reopen failed, restoring {} @ {}", oldPort, oldBaud);
    begin(oldPort, oldBaud, static_cast<int>(this->responseTimeout));
    return -1;
}

int Comms::setBaudRate(int baudRate){
    speed_t speed;
    if(!toSpeed(baudRate, speed)){
        SPDLOG_ERROR("Invalid baud rate specified: {}", baudRate);
        return -1;
    }
    if(serial.setBaudRate(speed) != 0){
        SPDLOG_ERROR("Could not switch {} to {} baud in place", serialPort, baudRate);
        return -1;
    }
    this->baudRateInt = baudRate;
    this->continousMode = false;
    SPDLOG_INFO("Serial port {} switched to {} baud in place", serialPort, baudRateInt);
    return 0;
}

void Comms::setSerialTaps(Serial::LineTap rx, Serial::LineTap tx){
    serial.setRxTap(std::move(rx));
    serial.setTxTap(std::move(tx));
}

void Comms::sendRaw(const std::string& line){
    serial.sendData(line + "\n");
}

bool Comms::sendDelaySearch(std::string& finalLine, unsigned timeoutMs,
                            const std::atomic<bool>* abort, bool* confirmed){
    //! With the firmware's default search settings (settings-eeprom.h: 0 to
    //! 500 us in 10 us steps) the firmware prints 51 step lines and one result
    //! line
    constexpr int expectedLines = 52;
    constexpr long lastStepUs = 500;   // SEARCH_DELAY_MAX_US
    constexpr unsigned samplesPerStep = 3;
    if(confirmed) *confirmed = false;

    //! Each step takes SEARCH_SAMPLES_PER_STEP (3) samples, and each sample
    //! waits for the next trigger (period = 1 / F) and then samples for H ms,
    //! so query the trigger rate and sampling time first ("F 0" changes nothing
    //! and reports both; "H 0" is the fallback for firmware without 'F') and
    //! derive the deadline and quiet window from them
    querySettings();   // learn F/H so the deadline matches the board
    //! each sample starts on a trigger edge, so round up to whole periods
    const unsigned periodMs = triggerPeriodMs();
    const unsigned sampleMs = ((samplingTimeMs + periodMs - 1u) / periodMs) * periodMs;
    const unsigned stepMs = samplesPerStep * std::max(sampleMs, periodMs);
    //! The stream counts as quiet after one step plus 1 s without a line
    //! (2.5 s at least)
    const auto searchQuiet = std::chrono::milliseconds(std::max(2500u, stepMs + 1000u));
    //! The caller's timeout is a floor: allow 51 steps plus 5 s
    timeoutMs = std::max(timeoutMs, (expectedLines - 1) * stepMs + 5000u);

    //! Leave continuous mode and drop stale lines, as sendNormalCommand() does
    this->continousMode = false;
    serial.clearReadBuffer();
    serial.sendData("D\n");
    SPDLOG_INFO("Sent D command to serial device (step ~{} ms, timeout {} ms)", stepMs, timeoutMs);

    auto now = std::chrono::steady_clock::now();
    auto deadline = now + std::chrono::milliseconds(timeoutMs);
    auto lastLine = now;
    int count = 0;
    long prevUs = -1;
    std::string line;
    while((now = std::chrono::steady_clock::now()) < deadline){
        if(abort && abort->load()) return false;
        if(serial.getReceived(line) && line.rfind(delaySearchMarker + ":", 0) == 0){
            finalLine = line;
            lastLine = now;
            ++count;
            //! The sweep is "confirmed" when the 52nd line arrives or when a
            //! line follows the 500 us (last) step: that line is the result
            if(count >= expectedLines || prevUs >= lastStepUs){
                SPDLOG_INFO("Delay search finished: {}", line);
                if(confirmed) *confirmed = true;
                return true;
            }
            //! Remember this step's delay, to recognise the line after the last
            //! step even if some step lines were missed
            long us = -1;
            prevUs = std::sscanf(line.c_str() + 2, "%ld", &us) == 1 ? us : -1;
            continue;
        }
        //! A finish by quiet window alone is unconfirmed: the firmware was built
        //! with other search settings, or lines were lost (Serial keeps only
        //! the latest line)
        if(count > 0 && now - lastLine > searchQuiet){
            SPDLOG_WARN("Delay search went quiet after {} lines (unconfirmed): {}", count, finalLine);
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    SPDLOG_WARN("Timeout during delay search ({} of {} lines)", count, expectedLines);
    return false;
}
