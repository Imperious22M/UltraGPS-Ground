cmake_minimum_required(VERSION 3.15...4.0)

project(UltraGpsXY VERSION 1.0
                    DESCRIPTION "UltraGPS XY Ground Station, exposes the base-station to commands from the network"
                    LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Build-mode option
#
#   Default (no flag):  SPDLOG_INFO / SPDLOG_WARN / SPDLOG_ERROR macros are
#                       compiled out entirely.
#                       spdlog::info() / spdlog::error() direct calls (banner,
#                       module-init messages) are always present regardless.
#
#   cmake -DVERBOSE_LOGGING=ON ..
#                       All SPDLOG_xxx macros are active (DEBUG level and above).
#                       Also sets CMAKE_BUILD_TYPE=Debug (no optimisation,
#                       NDEBUG not defined).
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

# Add the argparse library
add_subdirectory("${CMAKE_SOURCE_DIR}/argparse")

# ---------------------------------------------------------------------------
# Terminal UI (notcurses-core)
#
#   ULTRAGPS_TUI             Build the --tui interface (default ON). If
#                            notcurses cannot be obtained the build falls back
#                            to headless-only with a warning.
#   ULTRAGPS_NOTCURSES       Where notcurses comes from:
#     bundled (default)      Download notcurses ${ULTRAGPS_NOTCURSES_VERSION}
#                            (hash-verified tarball) at build time, apply
#                            cmake/NotcursesTmuxDa1.cmake, and link
#                            notcurses-core statically. Required for tmux:
#                            stock notcurses (3.0.9 and 3.0.17 tested)
#                            hangs at startup inside tmux >= 3.4.
#     system                 Use an installed notcurses-core found with
#                            pkg-config (static archive preferred). Only
#                            safe if that copy carries the same patch.
#   ULTRAGPS_NOTCURSES_SRC   Optional local notcurses source tree or tarball
#                            for offline builds (bundled mode).
#
#   The static link keeps the binary's NEEDED list to libc/libstdc++/libgcc_s/
#   libm/libtinfo/libz, which all belong to distro packages. dpkg-shlibdeps
#   can then resolve every entry, and nothing from /usr/local ends up in the
#   .deb. Build dependencies: pkg-config, libunistring-dev, libncurses-dev,
#   zlib1g-dev.
# ---------------------------------------------------------------------------
option(ULTRAGPS_TUI "Build the notcurses terminal UI (--tui)" ON)
set(ULTRAGPS_NOTCURSES "bundled" CACHE STRING "notcurses source: bundled or system")
set_property(CACHE ULTRAGPS_NOTCURSES PROPERTY STRINGS bundled system)
set(ULTRAGPS_NOTCURSES_SRC "" CACHE PATH "Local notcurses source dir or tarball (bundled mode, offline)")
set(ULTRAGPS_NOTCURSES_VERSION "3.0.17")
set(ULTRAGPS_NOTCURSES_SHA256 "b0fbe824984fe25b5a16770dbd00b85d44db5d09cc35bd881b95335d0db53128")

set(ULTRAGPS_TUI_DYNAMIC_NOTCURSES OFF)
if(ULTRAGPS_TUI)
    find_package(PkgConfig QUIET)
    find_package(Threads REQUIRED)
    find_library(UNISTRING_STATIC NAMES libunistring.a)
    find_library(UNISTRING_LIB NAMES unistring)
    find_library(TINFO_LIB NAMES tinfo ncursesw)
    find_library(ZLIB_LIB NAMES z)
    # Prefer the static libunistring: the distro soname has changed across
    # releases (.so.2 -> .so.5)
    if(UNISTRING_STATIC)
        set(_unistring ${UNISTRING_STATIC})
    else()
        set(_unistring ${UNISTRING_LIB})
    endif()

    if(ULTRAGPS_NOTCURSES STREQUAL "bundled")
        if(NOT PkgConfig_FOUND OR NOT _unistring OR NOT TINFO_LIB OR NOT ZLIB_LIB)
            message(WARNING "TUI: bundled notcurses needs pkg-config, libunistring-dev, "
                            "libncurses-dev and zlib1g-dev - building without the TUI")
            set(ULTRAGPS_TUI OFF)
        else()
            include(ExternalProject)
            if(ULTRAGPS_NOTCURSES_SRC AND IS_DIRECTORY "${ULTRAGPS_NOTCURSES_SRC}")
                set(_nc_source SOURCE_DIR "${ULTRAGPS_NOTCURSES_SRC}")
            elseif(ULTRAGPS_NOTCURSES_SRC)
                set(_nc_source URL "${ULTRAGPS_NOTCURSES_SRC}")
            else()
                set(_nc_source
                    URL "https://github.com/dankamongmen/notcurses/archive/refs/tags/v${ULTRAGPS_NOTCURSES_VERSION}.tar.gz"
                    URL_HASH "SHA256=${ULTRAGPS_NOTCURSES_SHA256}")
            endif()
            set(_nc_bin "${CMAKE_BINARY_DIR}/notcurses-build")
            set(_nc_lib "${_nc_bin}/libnotcurses-core.a")
            ExternalProject_Add(notcurses_ext
                ${_nc_source}
                PREFIX "${CMAKE_BINARY_DIR}/notcurses"
                BINARY_DIR "${_nc_bin}"
                DOWNLOAD_EXTRACT_TIMESTAMP TRUE
                PATCH_COMMAND ${CMAKE_COMMAND} -P "${CMAKE_SOURCE_DIR}/cmake/NotcursesTmuxDa1.cmake"
                CMAKE_ARGS
                    -DCMAKE_BUILD_TYPE=Release
                    -DUSE_MULTIMEDIA=none
                    -DUSE_PANDOC=OFF
                    -DUSE_DOCTEST=OFF
                    -DUSE_POC=OFF
                    -DUSE_CXX=OFF
                    -DUSE_STATIC=ON
                    -DUSE_DEFLATE=OFF
                    -DUSE_GPM=OFF
                    -DUSE_QRCODEGEN=OFF
                    -DBUILD_EXECUTABLES=OFF
                    -DBUILD_FFI_LIBRARY=OFF
                    -DBUILD_TESTING=OFF
                BUILD_COMMAND ${CMAKE_COMMAND} --build "${_nc_bin}" --target notcurses-core-static
                INSTALL_COMMAND ""
                BUILD_BYPRODUCTS "${_nc_lib}"
                LOG_CONFIGURE TRUE
                LOG_BUILD TRUE)
            ExternalProject_Get_Property(notcurses_ext SOURCE_DIR)
            add_library(ultragps_notcurses INTERFACE)
            # Plain INTERFACE library: the include dir only exists after download
            target_include_directories(ultragps_notcurses INTERFACE "${SOURCE_DIR}/include")
            target_link_libraries(ultragps_notcurses INTERFACE
                "${_nc_lib}" ${_unistring} ${TINFO_LIB} ${ZLIB_LIB} m Threads::Threads)
            message(STATUS "TUI: bundled notcurses-core ${ULTRAGPS_NOTCURSES_VERSION} (static, tmux patch)")
        endif()
    elseif(ULTRAGPS_NOTCURSES STREQUAL "system")
        if(PkgConfig_FOUND)
            pkg_check_modules(NOTCURSES QUIET notcurses-core>=3.0)
        endif()
        if(NOTCURSES_FOUND)
            find_library(NC_CORE_STATIC NAMES libnotcurses-core.a HINTS ${NOTCURSES_LIBRARY_DIRS})
            find_library(DEFLATE_LIB NAMES deflate)
            add_library(ultragps_notcurses INTERFACE)
            target_include_directories(ultragps_notcurses INTERFACE ${NOTCURSES_INCLUDE_DIRS})
            if(NC_CORE_STATIC AND _unistring AND TINFO_LIB)
                # Static notcurses may need libdeflate or zlib, depending on how it was built
                set(_nc_compress)
                if(DEFLATE_LIB)
                    list(APPEND _nc_compress ${DEFLATE_LIB})
                endif()
                if(ZLIB_LIB)
                    list(APPEND _nc_compress ${ZLIB_LIB})
                endif()
                target_link_libraries(ultragps_notcurses INTERFACE
                    ${NC_CORE_STATIC} ${_unistring} ${TINFO_LIB} ${_nc_compress} m Threads::Threads)
                message(STATUS "TUI: system notcurses-core ${NOTCURSES_VERSION} (static: ${NC_CORE_STATIC})")
            else()
                target_link_libraries(ultragps_notcurses INTERFACE ${NOTCURSES_LINK_LIBRARIES})
                set(ULTRAGPS_TUI_DYNAMIC_NOTCURSES ON)
                message(STATUS "TUI: system notcurses-core ${NOTCURSES_VERSION} (shared)")
            endif()
            message(WARNING "TUI: unpatched notcurses hangs on startup inside tmux >= 3.4; "
                            "prefer -DULTRAGPS_NOTCURSES=bundled")
        else()
            message(WARNING "TUI: notcurses-core not found via pkg-config - building without the TUI")
            set(ULTRAGPS_TUI OFF)
        endif()
    else()
        message(FATAL_ERROR "ULTRAGPS_NOTCURSES must be 'bundled' or 'system'")
    endif()
endif()

# Add the main executable along with its modules
add_executable(UltraGpsXY
    main.cpp
    SerialModule/serial.cpp
    ServerModule/serverModule.cpp
    CommsModule/comms.cpp
    CommsModule/commandHelp.cpp
    TuiModule/tuiState.cpp
)

if(ULTRAGPS_TUI)
    target_sources(UltraGpsXY PRIVATE
        TuiModule/tui.cpp
        TuiModule/art.cpp
        TuiModule/screens/mainScreen.cpp
        TuiModule/screens/settingsScreen.cpp
        TuiModule/screens/helpScreen.cpp
    )
    target_link_libraries(UltraGpsXY PRIVATE ultragps_notcurses)
    if(TARGET notcurses_ext)
        add_dependencies(UltraGpsXY notcurses_ext)
    endif()
    target_compile_definitions(UltraGpsXY PRIVATE ULTRAGPS_HAVE_TUI=1)
else()
    target_compile_definitions(UltraGpsXY PRIVATE ULTRAGPS_HAVE_TUI=0)
endif()

# Link the spdlog library to the target
target_link_libraries(UltraGpsXY PRIVATE spdlog::spdlog)

# SPDLOG_ACTIVE_LEVEL gates the SPDLOG_xxx macros at compile time.
# SPDLOG_LEVEL_OFF  -> every SPDLOG_xxx macro expands to nothing (no-op).
# SPDLOG_LEVEL_DEBUG -> all macros are active down to DEBUG severity.
# Direct calls such as spdlog::info() are unaffected and always compiled in.
target_compile_definitions(UltraGpsXY PRIVATE
    SPDLOG_ACTIVE_LEVEL=${SPDLOG_ACTIVE_LEVEL_VALUE}
)

# Link the argparse library to the target
target_link_libraries(UltraGpsXY PRIVATE argparse::argparse)

# ---------------------------------------------------------------------------
# Installation
#
#   Installs the ground-station binary to /usr/bin so it is on the default
#   PATH. The third-party libraries (spdlog, argparse) are compiled in
#   statically, so nothing else needs to be shipped.
# ---------------------------------------------------------------------------
install(TARGETS UltraGpsXY
    RUNTIME DESTINATION bin
)

# ---------------------------------------------------------------------------
# Debian (.deb) packaging via CPack
#
#   Build the package with:
#       cmake -S . -B build
#       cmake --build build
#       cpack --config build/CPackConfig.cmake -G DEB
#   producing e.g. ultragps-xy_<version>_amd64.deb in the build directory.
# ---------------------------------------------------------------------------
set(CPACK_GENERATOR "DEB")
set(CPACK_PACKAGE_NAME "ultragps-xy")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
if(ULTRAGPS_TUI)
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION} (with --tui terminal UI)")
else()
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
endif()
# CPackDeb uses the contact as the package maintainer
set(CPACK_PACKAGE_CONTACT "Please contact the engineering department at St. Mary's University")
set(CPACK_DEBIAN_PACKAGE_SECTION "science")
set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "amd64")

# Auto-resolve runtime dependencies (libc6, libstdc++6, libgcc-s1, ...) from
# the linked binary rather than hard-coding them.
#
# With the default bundled notcurses (ULTRAGPS_NOTCURSES=bundled, built with
# USE_DEFLATE=OFF) or a static system archive, the binary's NEEDED list is only
# libc/libstdc++/libgcc_s/libm/libtinfo/libz, all owned by packages, so
# shlibdeps works unchanged. A *shared* system notcurses
# (ULTRAGPS_NOTCURSES=system finding only a .so) is not packaged: the distro
# libnotcurses-core3 lacks the tmux fix, and a copy in /usr/local is owned by
# no package so dpkg-shlibdeps fails. build.sh deb refuses that case too.
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
if(ULTRAGPS_TUI_DYNAMIC_NOTCURSES)
    message(WARNING "notcurses is linked dynamically: 'cpack -G DEB' is not "
                    "supported for this build. Use the default "
                    "-DULTRAGPS_NOTCURSES=bundled to make a .deb.")
endif()

include(CPack)