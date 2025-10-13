#include "settings.h"
#include <fstream>
#include <spdlog/spdlog.h>
#include <iostream>
#include "defaultSettings.h"

SettingsModule::SettingsModule(std::string filename) : settingsFilename(std::move(filename)) {
    loadSettings();
}

void SettingsModule::saveSettings() {
    writeSettingsToFile(settingsFilename);
}

void SettingsModule::loadSettings() {
    readSettingsFromFile(settingsFilename);
}

void SettingsModule::writeSettingsToFile(const std::string& filename) {
    try {
        std::ofstream file(filename);
        file << settings_table << std::endl;
        SPDLOG_INFO("Settings saved to {}", filename);
    } catch (const std::exception& e) {
        SPDLOG_ERROR("Failed to save settings to {}: {}", filename, e.what());
    }
}

void SettingsModule::readSettingsFromFile(const std::string& filename) {
    try {
        settings_table = toml::parse_file(filename);
        SPDLOG_INFO("Settings loaded from {}", filename);
    } catch (const toml::parse_error& err) {
        SPDLOG_ERROR("Failed to parse settings file {}: {}", filename, err.description());
        SPDLOG_INFO("Reverting to default settings");
        initializeDefaultSettings();
    } catch (const std::exception& err) {
        SPDLOG_ERROR("Error loading settings file {}: {}", filename, err.what());
        SPDLOG_INFO("Reverting to default settings");
        initializeDefaultSettings();
    }
}

void SettingsModule::initializeDefaultSettings() {
    settings_table = toml::table{};
    settings_table.insert("serial_port", DEFAULT_SERIAL_PORT);
    settings_table.insert("number_of_receivers", static_cast<uint8_t>(DEFAULT_NUMBER_OF_RECEIVERS));
    settings_table.insert("calibration_reads", static_cast<uint8_t>(DEFAULT_CALIBRATION_READS));

    //! Initialize an all-zeroes array for receiver calibration distances
    toml::array receivers_array;
    for(size_t i = 0; i < DEFAULT_NUMBER_OF_RECEIVERS; ++i) {
        toml::table receiver;
        receiver.insert("id", static_cast<uint8_t>(i));

        toml::array dist_array;
        dist_array.push_back(0.0);
        dist_array.push_back(0.0);
        receiver.insert("cal_distances", dist_array);
        
        receivers_array.push_back(receiver);
    }
    settings_table.insert("receivers", receivers_array);

    //! Save to file
    writeSettingsToFile(settingsFilename);
}

// Convenience methods
void SettingsModule::setSerialPort(const std::string& port) {
    settings_table.insert_or_assign("serial_port", port);
    saveSettings();
}

bool SettingsModule::getSerialPort(std::string& returnString) const {
    auto serialPort = settings_table["serial_port"].value<std::string>();
    //! If serial port has value (serialPort.has_value() is tested implicitly)
    if(serialPort){
        returnString = *serialPort;
        return true;
    } else {
        returnString = "NOT_SET";
        return false;
    }
}

void SettingsModule::setNumberOfReceivers(uint8_t count) {
    settings_table.insert_or_assign("number_of_receivers", static_cast<uint8_t>(count));
    saveSettings();
}

bool SettingsModule::getNumberOfReceivers(uint8_t& returnValue) const {
    auto numberOfReceivers = settings_table["number_of_receivers"].value<uint8_t>();
    if(numberOfReceivers){
        returnValue = *numberOfReceivers;
        return true;
    } else {
        returnValue = 0;
        return false;
    }
}

void SettingsModule::setCalibrationReads(uint8_t reads) {
    settings_table.insert_or_assign("calibration_reads", static_cast<uint8_t>(reads));
    saveSettings();
}

bool SettingsModule::getCalibrationReads(uint8_t& returnValue) const {
    auto calibrationReads = settings_table["calibration_reads"].value<uint8_t>();
    if(calibrationReads){
        returnValue = *calibrationReads;
        return true;
    } else {
        returnValue = 0;
        return false;
    }
}

void SettingsModule::setCalibrationDistances(const std::vector<std::array<double, 2>>& distances) {
    toml::array receivers_array;

    //! Check if number of distances matches number of receivers
    if(distances.size() != settings_table["number_of_receivers"].value_or<uint8_t>(0)) {
        SPDLOG_WARN("Number of calibration distances ({}) does not match number of receivers ({}).",
                    distances.size(), settings_table["number_of_receivers"].value_or<size_t>(0));
       return; 
    }
    //! Create receiver entries
    for (size_t i = 0; i < distances.size(); ++i) {
        toml::table receiver;
        receiver.insert("id", static_cast<uint8_t>(i));
        
        toml::array dist_array;
        dist_array.push_back(distances[i][0]);
        dist_array.push_back(distances[i][1]);
        receiver.insert("cal_distances", dist_array);
        
        receivers_array.push_back(receiver);
    }

    settings_table.insert_or_assign("receivers", receivers_array);
    saveSettings();
}

//! TODO: Add error handling if receiver ID not found
std::vector<std::array<double, 2>> SettingsModule::getCalibrationDistances() const {
    std::vector<std::array<double, 2>> distances;
   
    if (auto receivers = settings_table["receivers"].as_array()) {
        for (const auto& receiver_node : *receivers) {
            if (auto receiver = receiver_node.as_table()) {
                if (auto dist_array = (*receiver)["cal_distances"].as_array()) {
                    if (dist_array->size() == 2) {
                        std::array<double, 2> dist = {
                            //! Get the values or default to 0.0 if not present
                            dist_array->at(0).value_or<double>(0.0),
                            dist_array->at(1).value_or<double>(0.0)
                        };
                        distances.push_back(dist);
                    }else{

                        SPDLOG_WARN("Calibration distances array at {} does not have exactly 2 elements.", 
                                    (*receiver)["id"].value_or<uint8_t>(-1));
                        distances.push_back({0.0, 0.0});
                    }
                }
            }
        }
    }
    
    return distances;
}

void SettingsModule::setReceiverOffset(size_t receiver_id, double slope, double intercept) {
    auto receivers = settings_table["receivers"].as_array();
    if (!receivers) {
        SPDLOG_ERROR("No receivers array found in TOML file");
        return;
    }
    
    // Find or create receiver entry
    toml::table* target_receiver = nullptr;
    for (auto& receiver_node : *receivers) {
        if (auto receiver = receiver_node.as_table()) {
            auto id = (*receiver)["id"].value<uint8_t>();
            if (id && *id == static_cast<uint8_t>(receiver_id)) {
                target_receiver = receiver;
                break;
            }
        }
    }
    
    //! Return an error if receiver is not found
    if (!target_receiver) {
        SPDLOG_ERROR("Receiver ID {} not found in TOML file", receiver_id);
        return; 
    }
    
    // Set offset data
    toml::table offset;
    offset.insert("slope", slope);
    offset.insert("intercept", intercept);
    target_receiver->insert_or_assign("offset", offset);
    
    saveSettings();
}

bool SettingsModule::getReceiverOffset(size_t receiver_id, double& slope, double& intercept) const {
    if (auto receivers = settings_table["receivers"].as_array()) {
        for (const auto& receiver_node : *receivers) {
            if (auto receiver = receiver_node.as_table()) {
                auto id = (*receiver)["id"].value<uint8_t>();
                if (id && *id == static_cast<uint8_t>(receiver_id) ) {
                    if (auto offset_table = (*receiver)["offset"].as_table()) {
                        auto slope_val = (*offset_table)["slope"].value<double>();
                        auto intercept_val = (*offset_table)["intercept"].value<double>();
                        
                        if (slope_val && intercept_val) {
                            slope = *slope_val;
                            intercept = *intercept_val;
                            return true;
                        }
                    }
                }
            }
        }
    }
    return false;
}

bool SettingsModule::hasCalibrationData() const {
    uint8_t numberOfReceivers = settings_table["number_of_receivers"].value_or<uint8_t>(0);
    uint8_t receiversWithOffsets = 0;
    if (auto receivers = settings_table["receivers"].as_array()) {
        for (const auto& receiver_node : *receivers) {
            if (auto receiver = receiver_node.as_table()) {
                if ((*receiver)["offset"].as_table()) {
                    receiversWithOffsets++;
                    if (receiversWithOffsets == numberOfReceivers) {
                        return true; // Found all receivers with offset data
                    }
                }
            }
        }
    }

    SPDLOG_WARN("Only {}/{} receivers have calibration offset data.", receiversWithOffsets, numberOfReceivers);
    return false;
}