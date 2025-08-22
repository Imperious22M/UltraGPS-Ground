cmake_minimum_required(VERSION 3.15...4.0)

project(UltraGpsXY VERSION 1.0
                    DESCRIPTION "Decode X and Y cords from UltraGPS"
                    LANGUAGES CXX)

# Set the C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Add the executable with source files
add_executable(UltraGpsXY
    main.cpp
    SerialModule/serial.cpp
)

# Include the directory for header files
target_include_directories(UltraGpsXY PRIVATE SerialModule)