cmake_minimum_required(VERSION 3.15...4.0)

project(UltraGpsXY VERSION 1.0
                    DESCRIPTION "Decode X and Y cords from UltraGPS"
                    LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Build-mode option
#
#   Default (no flag):  SPDLOG_INFO / SPDLOG_WARN / SPDLOG_ERROR macros are
#                       compiled out entirely — zero overhead in production.
#                       spdlog::info() / spdlog::error() direct calls (banner,
#                       module-init messages) are always present regardless.
#
#   cmake -DVERBOSE_LOGGING=ON ..
#                       All SPDLOG_xxx macros are active (DEBUG level and above).
#                       Also sets CMAKE_BUILD_TYPE=Debug and defines DEBUG so
#                       compiler optimisations are off and assert()s fire.
#
# ---------------------------------------------------------------------------
option(VERBOSE_LOGGING "Enable SPDLOG macro logging (SPDLOG_INFO, SPDLOG_DEBUG, etc.)" OFF)

if(VERBOSE_LOGGING)
    message(STATUS "VERBOSE_LOGGING=ON  — SPDLOG macros active, debug build")
    set(CMAKE_BUILD_TYPE Debug)
    set(SPDLOG_ACTIVE_LEVEL_VALUE SPDLOG_LEVEL_DEBUG)
else()
    message(STATUS "VERBOSE_LOGGING=OFF — SPDLOG macros compiled out (default)")
    # Keep whatever build type was requested; default to RelWithDebInfo so the
    # binary is optimised but still carries debug symbols for crash analysis.
    if(NOT CMAKE_BUILD_TYPE)
        set(CMAKE_BUILD_TYPE RelWithDebInfo)
    endif()
    set(SPDLOG_ACTIVE_LEVEL_VALUE SPDLOG_LEVEL_OFF)
endif()

# Set the C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Add the spdlog library
add_subdirectory("${CMAKE_SOURCE_DIR}/spdlog")

# Add the toml++ library
add_subdirectory("${CMAKE_SOURCE_DIR}/tomlplusplus")

# Add the argparse library
add_subdirectory("${CMAKE_SOURCE_DIR}/argparse")

# Add the main executable along with its modules
add_executable(UltraGpsXY
    main.cpp
    SerialModule/serial.cpp
    ServerModule/serverModule.cpp
    CommsModule/comms.cpp
)

# Include the directory for header files
target_include_directories(UltraGpsXY PRIVATE SerialModule)
target_include_directories(UltraGpsXY PRIVATE SettingsModule)
target_include_directories(UltraGpsXY PRIVATE CommsModule)

# Link the spdlog library to the target
target_link_libraries(UltraGpsXY PRIVATE spdlog::spdlog)

# SPDLOG_ACTIVE_LEVEL gates the SPDLOG_xxx macros at compile time.
# SPDLOG_LEVEL_OFF  → every SPDLOG_xxx macro expands to nothing (no-op).
# SPDLOG_LEVEL_DEBUG → all macros are active down to DEBUG severity.
# Direct calls such as spdlog::info() are unaffected and always compiled in.
target_compile_definitions(UltraGpsXY PRIVATE
    SPDLOG_ACTIVE_LEVEL=${SPDLOG_ACTIVE_LEVEL_VALUE}
)

if(VERBOSE_LOGGING)
    target_compile_definitions(UltraGpsXY PRIVATE DEBUG)
endif()

# Link the toml++ library to the target
target_link_libraries(UltraGpsXY PRIVATE tomlplusplus::tomlplusplus)

# Link the argparse library to the target
target_link_libraries(UltraGpsXY PRIVATE argparse::argparse)

# TODO
# Add Linux dpkg and binary support