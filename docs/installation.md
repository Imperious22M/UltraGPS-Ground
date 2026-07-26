# Installation

There are two ways to get `UltraGpsXY` onto a machine: build it from source, or
install the prebuilt Debian package.

## Prerequisites

- A C++17 compiler (GCC or Clang) and CMake ≥ 3.15
- `git` (the third-party libraries are git submodules)

## Build from source

Clone with submodules, then build with the helper script:

```bash
git clone --recurse-submodules https://github.com/Imperious22M/UltraGPS-Ground.git
cd UltraGPS-Ground

# If you already cloned without --recurse-submodules:
git submodule update --init --recursive

./build.sh build          # optimised binary → ./build/UltraGpsXY
./build.sh build --verbose  # debug build with SPDLOG macros active
```

`./build.sh clean` removes the `build/` directory (equivalent to `make clean`).

!!! note "Building without the helper script"
    `build.sh` just wraps CMake:

    ```bash
    cmake -S . -B build
    cmake --build build -j"$(nproc)"
    ```

## Debian package (.deb)

Build a `.deb` with the helper script:

```bash
./build.sh deb            # → ./build/ultragps-xy_<version>_amd64.deb
```

Install it:

```bash
sudo dpkg -i build/ultragps-xy_*_amd64.deb
```

This installs the binary to `/usr/bin/UltraGpsXY`, so it lands on your `PATH`.
Runtime dependencies (`libc6`, `libstdc++6`, `libgcc-s1`) are resolved
automatically by the package.

!!! warning "Packaging inside a Dropbox folder"
    The `deb` target intentionally builds in a temporary directory under `/tmp`
    rather than in place. This repository lives in a Dropbox-synced folder, and
    Dropbox's sync daemon races with the transient files `dpkg-shlibdeps`
    creates while packaging, corrupting the resulting `.deb`. Plain compilation
    is unaffected. See [Notes](notes.md) for details.
