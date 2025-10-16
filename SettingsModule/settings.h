//! file: Contains the class and helper routines to manage
//! the settings for the UltraGPS system

#include "../interfaces/settingsInterface.h"
#include <toml++/toml.h>
#include <string>
#include <vector>
#include <array>

#ifndef SETTINGSMODULE
#define SETTINGSMODULE

class SettingsModule : public SettingsModuleInterface {
public:
    // Initialize with optional filename
    SettingsModule(std::string filename = "config.toml");
   
    //! Save and load settings from file
    void saveSettings() override;
    void loadSettings() override;
    
    #ifdef DEBUG
    // Return the internal TOML (used for debugging)
    const toml::table& getSettingsTable() const { return settings_table; }
    #endif
    
    // General settings methods
    //! If the values are not set, return "NOT_SET" for string and some default value for integers
    void setSerialPort(const std::string& port) override;
    bool getSerialPort(std::string& returnString) const override;

    void setNumberOfReceivers(uint8_t count) override;
    bool getNumberOfReceivers(uint8_t& returnValue) const override;

    void setCalibrationReads(uint8_t reads) override;
    bool getCalibrationReads(uint8_t& returnValue) const override;

    void setUnits(const std::string& units) override;
    bool getUnits(std::string& returnString) const override;

    void setCalState(uint8_t state) override; //! 0: uncalibrated, 1: calibrated
    bool getCalState(uint8_t &state) const override;


    // Per-Receiver setting methods

    //! Takes an array which corresponds to the calibration distances for that receiver
    //! Index 0: distance to calibration point 1
    //! Index 1: distance to calibration point 2
    void setReceiverCalibrationDistances(size_t receiver_id, const std::array<double, 2>& distances);
    std::array<double, 2> getReceiverCalibrationDistances(size_t receiver_id);

    //! Sets the position of each receiver in the x/y plane
    //! Index 0: x position
    //! Index 1: y position
    void setReceiverPosition(size_t receiver_id, const std::array<double, 2>& position);
    std::array<double, 2> getReceiverPosition(size_t receiver_id);

    //! Takes slope and intercept for each receiver (usually set by calibration routine)
    //! Index 0: slope
    //! Index 1: intercept
    void setReceiverOffset(size_t receiver_id, const std::array<double, 2>& offsets);
    std::array<double, 2> getReceiverOffsets(size_t receiver_id);
    
    // Check if calibration data exists
    //! NOT IMPLEMENTED YET
    bool hasCalibrationData() const;
    
private:
    toml::table settings_table;
    std::string settingsFilename;
    
    // Helper functions
    void writeSettingsToFile(const std::string& filename);
    void readSettingsFromFile(const std::string& filename);
    void initializeDefaultSettings();
    toml::table* getReceiverById(size_t receiver_id);

};

#endif


