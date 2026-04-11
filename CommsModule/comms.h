/* Handles the interface between the serial communication and the UDP/TCP server.
*  Mainly deals with timing and synchonization problems that occur when handling
*   multi-threaded communications.
*
*  Also defines the set of valid ASCII commands that the Arduino base-station
*  firmware accepts, and provides validation helpers so that only recognised
*  commands are forwarded over serial.
*/

#include <string>
#include <unordered_map>
#include "../SerialModule/serial.h"
#include <spdlog/spdlog.h>
#include <termios.h>

//! Two set of command senders/receivers must be made, one for Normal mode and 
//! another for continouous mode.

class Comms{
public:

    //! Command classification returned by getCommandType().
    //! Normal  — one-shot commands (P, S) that expect a single "N: ..." response.
    //! Continous — streaming command (C) that switches to continuous mode.
    //! Invalid — unrecognised command; must be discarded.
    enum class CommandType { Normal, Continous, Invalid };

    int begin(std::string portName, int baudRate, int timeout = 500);

    int end();

    bool sendNormalCommand(std::string command, std::string &response);

    bool sendContinousCommand(std::string command);

    bool getContinousResponse(std::string &response);

    bool isContinousMode() const { return continousMode; }

    //! ---- Command validation API ----
    //! All three helpers accept the raw TCP string (e.g. "P\n") and work on it
    //! directly so that the caller does not need to strip whitespace first.

    //! Strip trailing newline/carriage-return and return the clean command string.
    std::string extractCommand(const std::string& rawCmd) const;

    //! Return true if the raw command maps to a known Arduino command.
    bool isValidCommand(const std::string& rawCmd) const;

    //! Return the CommandType for the raw command (Invalid if unrecognised).
    CommandType getCommandType(const std::string& rawCmd) const;

private:
    /* Response string buffer and timeout (ms) value*/
    std::string responseBuff;
    unsigned int responseTimeout;
    /* Flag to mark transition from continous mode to normal mode */
    bool continousMode = false;

    /* Serial module */
    Serial serial;
    speed_t baudRate; 
    uint32_t baudRateInt;
    std::string serialPort; 

    /*
    * Constant markers that mark if a response is from a continous
    * or normal command
    */
    const std::string normalCommandMarker = "N";
    const std::string continousCommandMarker = "C";

    /* Check response for marker and timeout if marker is not found */
    bool checkResponse(std::string &response, const std::string marker);

    //! Lookup table: command letter → CommandType.
    //! Matches the Arduino base-station GLOBAL_CMD_TABLE:
    //!   'P' → Normal  (PULSE)
    //!   'S' → Normal  (SIMULATE)
    //!   'C' → Continous (CONTINUOUS)
    const std::unordered_map<std::string, CommandType> validCommands = {
        { "P", CommandType::Normal    },
        { "S", CommandType::Normal    },
        { "C", CommandType::Continous },
    };
};
