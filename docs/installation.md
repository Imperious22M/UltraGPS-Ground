# Installation

There are two ways to get `UltraGpsXY` onto a machine: install the prebuilt
Debian package from a release, or build it from source.

## Install from a release (recommended)

Each tagged version is published on the
[Releases page](https://github.com/Imperious22M/UltraGPS-Ground/releases) with
the `.deb` package attached. Download the latest `ultragps-xy_<version>_amd64.deb`
and install it:

```bash
sudo dpkg -i ultragps-xy_*_amd64.deb
```

This installs the binary to `/usr/bin/UltraGpsXY` (on your `PATH`) and pulls in
its runtime dependencies (`libc6`, `libstdc++6`, `libgcc-s1`) automatically.

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

    ```
    cmake -S . -B build
    cmake --build build -j "$(nproc)"
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

!!! note "Packaging builds in an isolated directory"
    The `deb` target intentionally builds in a temporary directory under `/tmp`
    rather than in place, then copies the finished `.deb` into `./build/`. This
    isolates the transient files `dpkg-shlibdeps` creates while packaging from
    the working tree.
