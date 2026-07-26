#!/usr/bin/env bash
#
# build.sh — build helper for the UltraGPS ground-station program.
#
#   ./build.sh build      Compile the UltraGpsXY binary into ./build
#   ./build.sh deb        Build the installable .deb package
#   ./build.sh clean      Remove the ./build directory (like 'make clean')
#
# The 'deb' target is packaged OUTSIDE this directory (in a temp dir under
# /tmp)

set -euo pipefail

# Project root
PROJECT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$PROJECT_DIR/build"

VERBOSE=OFF

usage() {
    cat <<EOF
Usage: ./build.sh <command> [options]

Commands:
  build        Compile the UltraGpsXY binary into ./build
  deb          Build the .deb package (packaged in /tmp to avoid Dropbox
               corruption, then copied into ./build)
  clean        Remove the ./build directory (equivalent to 'make clean')

Options:
  -v, --verbose   Configure with -DVERBOSE_LOGGING=ON (build command only)
  -h, --help      Show this help

Examples:
  ./build.sh build            # optimised binary in ./build/UltraGpsXY
  ./build.sh build --verbose  # debug build with SPDLOG macros active
  ./build.sh deb              # ./build/ultragps-xy_<version>_amd64.deb
  ./build.sh clean            # wipe ./build
EOF
}

cmd_build() {
    echo ">> Configuring (VERBOSE_LOGGING=$VERBOSE) ..."
    cmake -S "$PROJECT_DIR" -B "$BUILD_DIR" -DVERBOSE_LOGGING="$VERBOSE"
    echo ">> Building ..."
    cmake --build "$BUILD_DIR" -j"$(nproc)"
    echo ">> Done: $BUILD_DIR/UltraGpsXY"
}

cmd_deb() {
    local version tmp deb
    # Build in a temporary directory.
    tmp="$(mktemp -d "${TMPDIR:-/tmp}/ultragps-deb.XXXXXX")"
    # Clean up the temp tree on exit, success or failure.
    trap 'rm -rf "$tmp"' RETURN

    echo ">> Configuring (release) in $tmp ..."
    cmake -S "$PROJECT_DIR" -B "$tmp" >/dev/null
    echo ">> Building ..."
    cmake --build "$tmp" -j"$(nproc)" >/dev/null
    echo ">> Packaging .deb ..."
    ( cd "$tmp" && cpack --config CPackConfig.cmake -G DEB >/dev/null )

    # Take the version from the package itself (authoritative — it comes from
    # PROJECT_VERSION in CMake) rather than re-parsing CMakeLists.txt.
    version="$(dpkg-deb -f "$tmp"/*.deb Version)"

    # Copy the finished package into ./build
    mkdir -p "$BUILD_DIR"
    deb="$BUILD_DIR/ultragps-xy_${version}_amd64.deb"
    cp "$tmp"/*.deb "$deb"
    echo ">> Done: $deb"
    echo
    dpkg-deb -I "$deb" | sed -n '/Package:/,/Description:/p'
}

cmd_clean() {
    if [[ -d "$BUILD_DIR" ]]; then
        echo ">> Removing $BUILD_DIR ..."
        rm -rf "$BUILD_DIR"
        echo ">> Clean."
    else
        echo ">> Nothing to clean ($BUILD_DIR does not exist)."
    fi
}

# ---- argument parsing -------------------------------------------------------
COMMAND=""
for arg in "$@"; do
    case "$arg" in
        build|deb|clean)  COMMAND="$arg" ;;
        -v|--verbose)     VERBOSE=ON ;;
        -h|--help)        usage; exit 0 ;;
        *) echo "Unknown argument: $arg" >&2; echo >&2; usage >&2; exit 1 ;;
    esac
done

case "$COMMAND" in
    build) cmd_build ;;
    deb)   cmd_deb ;;
    clean) cmd_clean ;;
    "")    usage >&2; exit 1 ;;
esac
