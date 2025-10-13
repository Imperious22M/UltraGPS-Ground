//! file: Contains the interface for the settings module

#include <string>

#ifndef SETTINGS_MODULE_INTERFACE
#define SETTINGS_MODULE_INTERFACE

class SettingsModuleInterface {
public:
    virtual ~SettingsModuleInterface() = default;

    virtual void saveSettings() = 0;
    virtual void loadSettings() = 0;

    //! Serial port setting
    virtual void setSerialPort(const std::string& port) = 0;
    virtual bool getSerialPort(std::string& returnString) const = 0;
    
    //! Number of receivers setting
    virtual void setNumberOfReceivers(uint8_t count) = 0;
    virtual bool getNumberOfReceivers(uint8_t& returnValue) const = 0;
   
    //! Calibration reads setting
    virtual void setCalibrationReads(uint8_t reads) = 0;
    virtual bool getCalibrationReads(uint8_t& returnValue) const = 0;
};

#endif // SETTINGS_MODULE_INTERFACE