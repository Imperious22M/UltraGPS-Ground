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
#   ULTRAGPS_TUI             Build the --tui interface (default ON).
#   ULTRAGPS_TUI_TERMINAL    Which terminal the TUI build targets:
#     bash (default)         Stock notcurses from the submodule, unmodified.
#                            For running in a plain terminal (bash/zsh).
#     tmux                   The same notcurses with
#                            notcurses-tmux-patch-cmake/NotcursesTmuxDa1.cmake
#                            applied. Stock notcurses (3.0.9 and 3.0.17
#                            tested) hangs at startup inside tmux >= 3.4.
#
#   notcurses comes from the `notcurses` git submodule, pinned to the commit
#   recorded in this repository. At build time the submodule is copied into
#   the build directory, patched there (tmux only), and notcurses-core is
#   built and linked statically. The submodule checkout is never modified.
#
#   The static link keeps the binary's NEEDED list to libc/libstdc++/libgcc_s/
#   libm/libtinfo/libz, which all belong to distro packages. dpkg-shlibdeps
#   can then resolve every entry, and nothing from /usr/local ends up in the
#   .deb. Build dependencies: libunistring-dev, libncurses-dev, zlib1g-dev.
# ---------------------------------------------------------------------------
option(ULTRAGPS_TUI "Build the notcurses terminal UI (--tui)" ON)
set(ULTRAGPS_TUI_TERMINAL "bash" CACHE STRING "Terminal the TUI targets: bash or tmux")
set_property(CACHE ULTRAGPS_TUI_TERMINAL PROPERTY STRINGS bash tmux)

if(ULTRAGPS_TUI)
    if(NOT ULTRAGPS_TUI_TERMINAL MATCHES "^(bash|tmux)$")
        message(FATAL_ERROR "ULTRAGPS_TUI_TERMINAL must be 'bash' or 'tmux'")
    endif()

    set(_nc_submodule "${CMAKE_SOURCE_DIR}/notcurses")
    if(NOT EXISTS "${_nc_submodule}/src/lib/in.c")
        message(FATAL_ERROR "TUI: the notcurses submodule is not checked out. Run\n"
                            "    git submodule update --init --recursive\n"
                            "or configure with -DULTRAGPS_TUI=OFF (./build.sh --no-tui).")
    endif()

    # The submodule must be at the commit this repository pins (the gitlink in
    # the index). A stale or hand-moved checkout would build another version.
    find_package(Git QUIET)
    set(_nc_commit "")
    if(GIT_FOUND AND EXISTS "${CMAKE_SOURCE_DIR}/.git")
        execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${_nc_submodule}" rev-parse HEAD
                        OUTPUT_VARIABLE _nc_commit OUTPUT_STRIP_TRAILING_WHITESPACE
                        RESULT_VARIABLE _nc_rc ERROR_QUIET)
        execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${CMAKE_SOURCE_DIR}" ls-files -s notcurses
                        OUTPUT_VARIABLE _nc_pin OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
        string(REGEX MATCH "[0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f]+" _nc_pin "${_nc_pin}")
        if(_nc_rc EQUAL 0 AND _nc_pin AND NOT _nc_commit STREQUAL _nc_pin)
            message(FATAL_ERROR "TUI: notcurses submodule is at ${_nc_commit}, expected ${_nc_pin}. Run\n"
                                "    git submodule update --init notcurses")
        endif()
        # Re-run configure when the submodule moves, so the copy below is redone
        execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${_nc_submodule}" rev-parse --absolute-git-dir
                        OUTPUT_VARIABLE _nc_gitdir OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
        if(EXISTS "${_nc_gitdir}/HEAD")
            set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_nc_gitdir}/HEAD")
        endif()
    endif()
    if(NOT _nc_commit)
        set(_nc_commit "unknown")
    endif()
    string(SUBSTRING "${_nc_commit}" 0 7 _nc_short)

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
    if(NOT _unistring OR NOT TINFO_LIB OR NOT ZLIB_LIB)
        message(FATAL_ERROR "TUI: notcurses needs libunistring-dev, libncurses-dev and zlib1g-dev. "
                            "Install them, or configure with -DULTRAGPS_TUI=OFF (./build.sh --no-tui).")
    endif()

    if(ULTRAGPS_TUI_TERMINAL STREQUAL "tmux")
        set(_nc_patch ${CMAKE_COMMAND} -P "${CMAKE_SOURCE_DIR}/notcurses-tmux-patch-cmake/NotcursesTmuxDa1.cmake")
    else()
        set(_nc_patch "")
    endif()

    # One source copy and build tree per variant and commit, so switching
    # either never reuses a stale (un)patched tree.
    set(_nc_prefix "${CMAKE_BINARY_DIR}/notcurses-${ULTRAGPS_TUI_TERMINAL}-${_nc_short}")
    set(_nc_src "${_nc_prefix}/src")
    set(_nc_bin "${_nc_prefix}/build")
    set(_nc_lib "${_nc_bin}/libnotcurses-core.a")
    include(ExternalProject)
    ExternalProject_Add(notcurses_ext
        PREFIX "${_nc_prefix}"
        SOURCE_DIR "${_nc_src}"
        BINARY_DIR "${_nc_bin}"
        # "Download" = copy the submodule checkout, so the patch never touches it
        DOWNLOAD_COMMAND ${CMAKE_COMMAND} -E copy_directory "${_nc_submodule}" "${_nc_src}"
        PATCH_COMMAND ${_nc_patch}
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
    add_library(ultragps_notcurses INTERFACE)
    # The headers are the same in both variants (the patch only touches in.c)
    target_include_directories(ultragps_notcurses INTERFACE "${_nc_submodule}/include")
    target_link_libraries(ultragps_notcurses INTERFACE
        "${_nc_lib}" ${_unistring} ${TINFO_LIB} ${ZLIB_LIB} m Threads::Threads)
    add_dependencies(ultragps_notcurses notcurses_ext)
    message(STATUS "TUI: notcurses-core ${_nc_short} from submodule, "
                   "${ULTRAGPS_TUI_TERMINAL} variant (static)")
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
    add_dependencies(UltraGpsXY notcurses_ext)
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
#   producing e.g. ultragps-xy_<version>_amd64.deb in the build directory
#   (ultragps-xy-tmux_... with -DULTRAGPS_TUI_TERMINAL=tmux).
# ---------------------------------------------------------------------------
set(CPACK_GENERATOR "DEB")
# The tmux variant is a separate package that replaces the default one (both
# install /usr/bin/UltraGpsXY), so its .deb can sit next to the bash one.
if(ULTRAGPS_TUI AND ULTRAGPS_TUI_TERMINAL STREQUAL "tmux")
    set(CPACK_PACKAGE_NAME "ultragps-xy-tmux")
    set(CPACK_DEBIAN_PACKAGE_CONFLICTS "ultragps-xy")
    set(CPACK_DEBIAN_PACKAGE_REPLACES "ultragps-xy")
    set(CPACK_DEBIAN_PACKAGE_PROVIDES "ultragps-xy")
else()
    set(CPACK_PACKAGE_NAME "ultragps-xy")
endif()
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
if(ULTRAGPS_TUI)
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
        "${PROJECT_DESCRIPTION} (with --tui terminal UI, ${ULTRAGPS_TUI_TERMINAL} build)")
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
# notcurses-core is built from the submodule with USE_DEFLATE=OFF and linked
# statically, so the binary's NEEDED list is only
# libc/libstdc++/libgcc_s/libm/libtinfo/libz, all owned by packages, and
# shlibdeps works unchanged.
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)

include(CPack)