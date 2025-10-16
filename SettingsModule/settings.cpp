#include "settings.h"
#include <fstream>
#include <spdlog/spdlog.h>
#include <iostream>
#include "defaultSettings.h"

SettingsModule::SettingsModule(std::string filename) : settingsFilename(std::move(filename)) {
    loadSettings();
}

//! Load and save methods
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
    settings_table.insert("units", DEFAULT_UNITS);
    settings_table.insert("cal_state", static_cast<uint8_t>(0)); // 0: uncalibrated, 1: calibrated

    //! Default settings for all receivers
    toml::array receivers_array;
    for(size_t i = 0; i < DEFAULT_NUMBER_OF_RECEIVERS; ++i) {
        toml::table receiver;   //! Individual receiver ID
        receiver.insert("id", static_cast<uint8_t>(i));

        toml::array dist_array; //! Calibration distances array
        dist_array.push_back(0.0);
        dist_array.push_back(0.0);
        receiver.insert("cal_distances", dist_array);

        toml::table offsets_table; //! Calibration offsets table
        offsets_table.insert("slope", 1.0);
        offsets_table.insert("intercept", 0.0);
        receiver.insert_or_assign("offset", offsets_table);

        toml::array position_array; //! Receiver position array (location in the grid)
        position_array.push_back(0.0);
        position_array.push_back(0.0);
        receiver.insert("position", position_array);
        receivers_array.push_back(receiver);
    }
    settings_table.insert("receivers", receivers_array);

    //! Save to file
    writeSettingsToFile(settingsFilename);
}

//! Internal class management methods
toml::table* SettingsModule::getReceiverById(size_t receiver_id) {
    if (auto receivers = settings_table["receivers"].as_array()) {
        for (auto& receiver_node : *receivers) {
            if (auto receiver = receiver_node.as_table()) {
                auto id = (*receiver)["id"].value<uint8_t>();
                if (id && *id == static_cast<uint8_t>(receiver_id)) {
                    return receiver;
                }
            }
        }
    }
    return nullptr; // Return empty pointer if table is not found
}

// General settings methods
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

void SettingsModule::setUnits(const std::string& units) {
    settings_table.insert_or_assign("units", units);
    saveSettings();
}

bool SettingsModule::getUnits(std::string& returnString) const {
    auto units = settings_table["units"].value<std::string>();
    if(units){
        returnString = *units;
        return true;
    } else {
        returnString = "NOT_SET";
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

void SettingsModule::setCalState(uint8_t state) {
    settings_table.insert_or_assign("cal_state", state);
    saveSettings();
}

bool SettingsModule::getCalState(uint8_t &state) const {
    auto calState = settings_table["cal_state"].value<uint8_t>();
    if(calState){
        state = *calState;
        return true;
    } else {
        state = 0;
        return false;
    }
}

//! Per-Receiver setting methods
void SettingsModule::setReceiverCalibrationDistances(size_t receiver_id, const std::array<double, 2>& distances) {
    auto receiver = getReceiverById(receiver_id);
    if (!receiver) {
        SPDLOG_ERROR("Receiver ID {} not found in TOML file", receiver_id);
        return; 
    }

    toml::array dist_array;
    dist_array.push_back(distances[0]);
    dist_array.push_back(distances[1]);
    (*receiver).insert_or_assign("cal_distances", dist_array);

    saveSettings();
}

std::array<double, 2> SettingsModule::getReceiverCalibrationDistances(size_t receiver_id){
    auto receiver = const_cast<toml::table*>(getReceiverById(receiver_id));
    std::array<double, 2> distances = {0.0, 0.0};
    if (!receiver) {
        SPDLOG_ERROR("Receiver ID {} not found in TOML file", receiver_id);
        return distances; 
    }

    distances = {(*receiver)["cal_distances"][0].value_or<double>(0.0),
                 (*receiver)["cal_distances"][1].value_or<double>(0.0)};
    return distances;
}

void SettingsModule::setReceiverOffset(size_t receiver_id, const std::array<double, 2>& offsets) {
   
    auto receiver = const_cast<toml::table*>(getReceiverById(receiver_id));
    if (!receiver) {
        SPDLOG_ERROR("Receiver ID {} not found in TOML file", receiver_id);
        return;
    }

    // Set offset data
    toml::table offset;
    offset.insert("slope", offsets[0]);
    offset.insert("intercept", offsets[1]);
    receiver->insert_or_assign("offset", offset);
    
    saveSettings();
}

std::array<double, 2> SettingsModule::getReceiverOffsets(size_t receiver_id) {
    auto receiver = getReceiverById(receiver_id);
    std::array<double, 2> offset = {0.0, 0.0};
    if (!receiver) {
        SPDLOG_ERROR("Receiver ID {} not found in TOML file", receiver_id);
        return offset;
    }

    if (auto offset_table = (*receiver)["offset"].as_table()) {
        offset[0] = (*offset_table)["slope"].value_or<double>(0.0);
        offset[1] = (*offset_table)["intercept"].value_or<double>(0.0);
    }
    return offset;
}

void SettingsModule::setReceiverPosition(size_t receiver_id, const std::array<double, 2>& position){
    auto receiver = getReceiverById(receiver_id);
    if (!receiver) {
        SPDLOG_ERROR("Receiver ID {} not found in TOML file", receiver_id);
        return;
    }

    toml::array position_array;
    position_array.push_back(position[0]);
    position_array.push_back(position[1]);
    (*receiver).insert_or_assign("position", position_array);

    saveSettings();

}

std::array<double, 2> SettingsModule::getReceiverPosition(size_t receiver_id){
    auto receiver = getReceiverById(receiver_id);
    std::array<double, 2> position = {0.0, 0.0};
    if (!receiver) {
        SPDLOG_ERROR("Receiver ID {} not found in TOML file", receiver_id);
        return position;
    }

    position = {(*receiver)["position"][0].value_or<double>(0.0),
                (*receiver)["position"][1].value_or<double>(0.0)};
    return position;
}


//! Extra check methods
bool SettingsModule::hasCalibrationData() const {

    //! TO BE IMPLEMENTED
    return false;
}