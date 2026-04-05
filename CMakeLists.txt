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

# Add the toml++ library
add_subdirectory("${CMAKE_SOURCE_DIR}/tomlplusplus")

# Add the argpase library
add_subdirectory("${CMAKE_SOURCE_DIR}/argparse")

# Add the main executable along with its modules
add_executable(UltraGpsXY
    main.cpp
    SerialModule/serial.cpp
    ServerModule/serverModule.cpp
)

# Include the directory for header files
target_include_directories(UltraGpsXY PRIVATE SerialModule)
target_include_directories(UltraGpsXY PRIVATE interfaces)
target_include_directories(UltraGpsXY PRIVATE SettingsModule)

# Link the spdlog library to the target
target_link_libraries(UltraGpsXY PRIVATE spdlog::spdlog)
# Define SPDLOG_ACTIVE_LEVEL for the target (This controls what LOG LEVEL gets printed)
target_compile_definitions(UltraGpsXY PRIVATE SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_DEBUG)

# Link the toml++ library to the target
target_link_libraries(UltraGpsXY PRIVATE tomlplusplus::tomlplusplus)

# Link the argparse library to the target
target_link_libraries(UltraGpsXY PRIVATE argparse::argparse)

# Define DEBUG for the target
target_compile_definitions(UltraGpsXY PRIVATE DEBUG)

# TODO
# Add Linux dpkg and binary support