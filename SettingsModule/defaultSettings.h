//! file: Contains the default settings and parameters
//! for the UltraGPS system

//! These settings are simply a blank template and will not work to make a functional system,
//! as they do not contain any calibration data. The user must run the calibration routine
//! to generate the calibration data for their specific setup.

#include <array>
#include <vector>

#ifndef DEFAULTSETTINGSMODULE
#define DEFAULTSETTINGSMODULE

#define DEFAULT_SERIAL_PORT "/dev/ttyACM0"
#define DEFAULT_NUMBER_OF_RECEIVERS 6
#define DEFAULT_CALIBRATION_READS 5

#endif