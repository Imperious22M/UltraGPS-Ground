/* Handles the interface between the serial communication and the UDP/TCP server.
*  Mainly deals with timing and synchonization problems that occur when handling
*   multi-threaded communications
*/

#include <string>
#include "../SerialModule/serial.h"
#include <spdlog/spdlog.h>
#include <termios.h>

//! Two set of command senders/receivers must be made, one for Normal mode and 
//! another for continouous mode.

class Comms{
public:

    int begin(std::string portName, int baudRate, int timeout = 500);

    int end();

    bool sendNormalCommand(std::string command, std::string &response);

    bool sendContinousCommand(std::string command);

    bool getContinousResponse(std::string &response);

    bool isContinousMode() const { return continousMode; }

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
};
