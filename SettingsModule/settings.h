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
    
    void saveSettings() override;
    void loadSettings() override;
    
    #ifdef DEBUG
    // Return the internal TOML (used for debugging)
    const toml::table& getSettingsTable() const { return settings_table; }
    #endif
    
    // Convenience methods for common settings
    //! If the values are not set, return "NOT_SET" for string and a default value for integers
    void setSerialPort(const std::string& port) override;
    bool getSerialPort(std::string& returnString) const override;

    void setNumberOfReceivers(uint8_t count) override;
    bool getNumberOfReceivers(uint8_t& returnValue) const override;

    void setCalibrationReads(uint8_t reads) override;
    bool getCalibrationReads(uint8_t& returnValue) const override;

    // Returns a vector of arrays, each containing two doubles for the calibration distances
    // The index is the receiver ID
    void setCalibrationDistances(const std::vector<std::array<double, 2>>& distances);
    std::vector<std::array<double, 2>> getCalibrationDistances() const;
    
    void setReceiverOffset(size_t receiver_id, double slope, double intercept);
    bool getReceiverOffset(size_t receiver_id, double& slope, double& intercept) const;
    
    // Check if calibration data exists
    bool hasCalibrationData() const;
    
private:
    toml::table settings_table;
    std::string settingsFilename;
    
    // Helper functions for saving/loading settings
    void writeSettingsToFile(const std::string& filename);
    void readSettingsFromFile(const std::string& filename);
    void initializeDefaultSettings();
};

#endif


