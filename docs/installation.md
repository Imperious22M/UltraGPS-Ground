# Installation

There are two ways to get `UltraGpsXY` onto a machine: install the prebuilt
Debian package from a release, or build it from source.

## Install from a release (recommended)

Each tagged version is published on the
[Releases page](https://github.com/Imperious22M/UltraGPS-Ground/releases) with
two `.deb` packages attached:

- `ultragps-xy_<version>_amd64.deb`: the default build, for a plain terminal.
- `ultragps-xy-tmux_<version>_amd64.deb`: the tmux build. Use it if you run
  `UltraGpsXY --tui` inside tmux 3.4 or newer. It replaces `ultragps-xy`.

Download the one you need and install it:

```bash
sudo dpkg -i ultragps-xy_*_amd64.deb
```

This installs the binary to `/usr/bin/UltraGpsXY` (on your `PATH`) and pulls in
its runtime dependencies (`libc6`, `libstdc++6`, `libgcc-s1`) automatically.

## Prerequisites

- A C++17 compiler (GCC or Clang) and CMake ≥ 3.15
- `git` (the third-party libraries are git submodules)
- For the terminal UI (built in by default): `sudo apt install libunistring-dev libncurses-dev zlib1g-dev`.
  notcurses comes from the `notcurses` submodule, so no network access is
  needed at build time. Without these packages configuring fails;
  `./build.sh build --no-tui` builds without the TUI.

## Build from source

Clone with submodules, then build with the helper script:

```bash
git clone --recurse-submodules https://github.com/Imperious22M/UltraGPS-Ground.git
cd UltraGPS-Ground

# If you already cloned without --recurse-submodules:
git submodule update --init --recursive

./build.sh build          # optimised binary → ./build/UltraGpsXY (bash TUI)
./build.sh build --tmux   # TUI that also starts inside tmux
./build.sh build --verbose  # debug build with SPDLOG macros active
```

The default **bash** variant uses stock notcurses. Stock notcurses hangs at
startup inside tmux 3.4 or newer, so build with `--tmux` (which applies the
patch in `notcurses-tmux-patch-cmake/`) if you run the TUI inside tmux.

`./build.sh clean` removes the `build/` directory (equivalent to `make clean`).

!!! note "Building without the helper script"
    `build.sh` just wraps CMake:

    ```
    cmake -S . -B build                               # bash variant
    cmake -S . -B build -DULTRAGPS_TUI_TERMINAL=tmux  # or the tmux variant
    cmake --build build -j "$(nproc)"
    ```

## Debian package (.deb)

Build a `.deb` with the helper script:

```bash
./build.sh deb            # → ./build/ultragps-xy_<version>_amd64.deb
./build.sh deb --tmux     # → ./build/ultragps-xy-tmux_<version>_amd64.deb
```

`ultragps-xy-tmux` installs the same `/usr/bin/UltraGpsXY` and replaces
`ultragps-xy`, so only one of the two can be installed.

Install it:

```bash
sudo dpkg -i build/ultragps-xy_*_amd64.deb
```

This installs the binary to `/usr/bin/UltraGpsXY`, so it lands on your `PATH`.
Runtime dependencies (`libc6`, `libstdc++6`, `libgcc-s1`) are resolved
automatically by the package.

!!! note "Packaging builds in an isolated directory"
    The `deb` target intentionally builds in a temporary directory under `/tmp`
    rather than in place, then copies the finished `.deb` into `./build/`. This
    isolates the transient files `dpkg-shlibdeps` creates while packaging from
    the working tree.
