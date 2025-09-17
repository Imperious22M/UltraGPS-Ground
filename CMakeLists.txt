cmake_minimum_required(VERSION 3.15...4.0)

project(UltraGpsXY VERSION 1.0
                    DESCRIPTION "Decode X and Y cords from UltraGPS"
                    LANGUAGES CXX)


# Set debug flags for building during development phase
set(CMAKE_BUILD_TYPE Debug)

# Set the C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Add the spdlog library
add_subdirectory("${CMAKE_SOURCE_DIR}/spdlog")

# Add the main executable with source files
add_executable(UltraGpsXY
    main.cpp
    SerialModule/serial.cpp
    CordDecoder/cordDecoder.cpp
)

# Include the directory for header files
target_include_directories(UltraGpsXY PRIVATE SerialModule)
target_include_directories(UltraGpsXY PRIVATE interfaces)
target_include_directories(UltraGpsXY PRIVATE CordDecoder)
# Add spdlog to the target
target_link_libraries(UltraGpsXY PRIVATE spdlog::spdlog)

# TODO
# Add windows binary suport
# Add Linux dpkg and binary support