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
SITE_DIR="$PROJECT_DIR/site"

VERBOSE=OFF
TUI=ON
TERMINAL=bash

usage() {
    cat <<EOF
Usage: ./build.sh <command> [options]

Commands:
  build        Compile the UltraGpsXY binary into ./build
  deb          Build the .deb package in an isolated /tmp dir, then copy
               it into ./build
  clean        Remove the ./build directory (equivalent to 'make clean')
  docs         Preview the documentation locally with live reload
               (mkdocs serve — http://127.0.0.1:8000, Ctrl-C to stop)
  docs-build   Build the static documentation site into ./site

Options:
  -v, --verbose   Configure with -DVERBOSE_LOGGING=ON (build command only)
      --bash      Build the TUI for a plain terminal: stock notcurses
                  (-DULTRAGPS_TUI_TERMINAL=bash, the default)
      --tmux      Build the TUI for tmux: notcurses with the tmux patch
                  (-DULTRAGPS_TUI_TERMINAL=tmux)
      --no-tui    Build without the notcurses terminal UI (-DULTRAGPS_TUI=OFF)
  -h, --help      Show this help

Examples:
  ./build.sh build            # optimised binary in ./build/UltraGpsXY
  ./build.sh build --verbose  # debug build with SPDLOG macros active
  ./build.sh build --tmux     # TUI that works inside tmux
  ./build.sh deb              # ./build/ultragps-xy_<version>_amd64.deb
  ./build.sh deb --tmux       # ./build/ultragps-xy-tmux_<version>_amd64.deb
  ./build.sh clean            # wipe ./build
  ./build.sh docs             # live-preview docs without pushing
  ./build.sh docs-build       # render docs to ./site
EOF
}

# Ensure mkdocs is available before running a docs command; print an install
# hint otherwise.
require_mkdocs() {
    if ! command -v mkdocs >/dev/null 2>&1; then
        echo "error: 'mkdocs' is not installed." >&2
        echo "Install the documentation dependencies with:" >&2
        echo "    pip install -r docs/requirements.txt" >&2
        exit 1
    fi
}

# The TUI builds notcurses from the submodule; check it is checked out before
# configuring, with a clearer hint than CMake's.
require_notcurses() {
    if [[ "$TUI" == ON && ! -f "$PROJECT_DIR/notcurses/src/lib/in.c" ]]; then
        echo ">> Fetching the notcurses submodule ..."
        git -C "$PROJECT_DIR" submodule update --init --recursive notcurses
    fi
}

cmd_build() {
    require_notcurses
    echo ">> Configuring (VERBOSE_LOGGING=$VERBOSE, ULTRAGPS_TUI=$TUI, ULTRAGPS_TUI_TERMINAL=$TERMINAL) ..."
    cmake -S "$PROJECT_DIR" -B "$BUILD_DIR" -DVERBOSE_LOGGING="$VERBOSE" \
        -DULTRAGPS_TUI="$TUI" -DULTRAGPS_TUI_TERMINAL="$TERMINAL"
    echo ">> Building ..."
    cmake --build "$BUILD_DIR" -j"$(nproc)"
    echo ">> Done: $BUILD_DIR/UltraGpsXY"
}

cmd_deb() {
    local name version tmp deb
    require_notcurses
    # Build in a temporary directory.
    tmp="$(mktemp -d "${TMPDIR:-/tmp}/ultragps-deb.XXXXXX")"
    # Remove the temp tree when cmd_deb returns (an error exit leaves it behind).
    trap 'rm -rf "$tmp"' RETURN

    echo ">> Configuring (release, ULTRAGPS_TUI=$TUI, ULTRAGPS_TUI_TERMINAL=$TERMINAL) in $tmp ..."
    cmake -S "$PROJECT_DIR" -B "$tmp" -DULTRAGPS_TUI="$TUI" -DULTRAGPS_TUI_TERMINAL="$TERMINAL" >/dev/null
    echo ">> Building ..."
    cmake --build "$tmp" -j"$(nproc)" >/dev/null
    # The package must not depend on a notcurses shared library: it is linked
    # statically, and a copy in /usr/local is owned by no package.
    if readelf -d "$tmp/UltraGpsXY" | grep -E 'NEEDED.*(notcurses|unistring)' >&2; then
        echo "error: UltraGpsXY links notcurses/libunistring dynamically; the .deb would not install cleanly" >&2
        exit 1
    fi
    echo ">> Packaging .deb ..."
    ( cd "$tmp" && cpack --config CPackConfig.cmake -G DEB >/dev/null )

    # Take the name and version from the package itself (authoritative — they
    # come from CMake) rather than re-parsing CMakeLists.txt.
    name="$(dpkg-deb -f "$tmp"/*.deb Package)"
    version="$(dpkg-deb -f "$tmp"/*.deb Version)"

    # Copy the finished package into ./build
    mkdir -p "$BUILD_DIR"
    deb="$BUILD_DIR/${name}_${version}_amd64.deb"
    cp "$tmp"/*.deb "$deb"
    echo ">> Done: $deb"
    echo
    dpkg-deb -I "$deb" | sed -n '/Package:/,/Description:/p'
}

cmd_docs() {
    require_mkdocs
    echo ">> Serving docs at http://127.0.0.1:8000  (Ctrl-C to stop) ..."
    ( cd "$PROJECT_DIR" && mkdocs serve )
}

cmd_docs_build() {
    require_mkdocs
    echo ">> Building static docs into $SITE_DIR ..."
    ( cd "$PROJECT_DIR" && mkdocs build --site-dir "$SITE_DIR" )
    echo ">> Done: $SITE_DIR/index.html"
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
        build|deb|clean|docs|docs-build)  COMMAND="$arg" ;;
        -v|--verbose)     VERBOSE=ON ;;
        --bash)           TERMINAL=bash ;;
        --tmux)           TERMINAL=tmux ;;
        --no-tui)         TUI=OFF ;;
        -h|--help)        usage; exit 0 ;;
        *) echo "Unknown argument: $arg" >&2; echo >&2; usage >&2; exit 1 ;;
    esac
done

case "$COMMAND" in
    build)      cmd_build ;;
    deb)        cmd_deb ;;
    clean)      cmd_clean ;;
    docs)       cmd_docs ;;
    docs-build) cmd_docs_build ;;
    "")         usage >&2; exit 1 ;;
esac
