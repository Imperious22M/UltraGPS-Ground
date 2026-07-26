# Developer's Guide

A self-contained reference for developers who **fork the repository** and need
to work with the project's tooling — building, packaging, previewing the
documentation, and cutting releases. Everything needed is on this page.

## Overview

`UltraGpsXY` is a single C++17 executable that runs on the ground-station host
and bridges the positioning hardware to the network:

- Maintains a **serial link** to the base-station Arduino (default
  `/dev/ttyACM0` @ 38400 bps) via the Comms/Serial modules.
- Runs a **TCP command server** (default port `9000`): network clients send
  commands, which are validated and forwarded to the Arduino, and the response
  is returned to the client.
- Runs a **UDP data stream** (default port `9001`): while in continuous mode,
  live position data from the Arduino is streamed out to clients.

The third-party libraries (`spdlog`, `tomlplusplus`, `argparse`) are vendored as
git submodules and statically linked, so the installed binary has no
third-party runtime dependencies.

## Requirements

- A C++17 compiler (GCC or Clang) and CMake ≥ 3.15
- `git` (the third-party libraries are git submodules)
- `dpkg`/`cpack` — only needed to build the `.deb` package
- Python 3 with `pip` — only needed to build/preview the documentation

Clone with submodules:

```bash
git clone --recurse-submodules https://github.com/Imperious22M/UltraGPS-Ground.git
cd UltraGPS-Ground
# If you already cloned without --recurse-submodules:
git submodule update --init --recursive
```

## Installing a prebuilt release

You don't need to build anything to run the program — each release ships a
prebuilt Debian package. Download the latest `.deb` from the
[Releases page](https://github.com/Imperious22M/UltraGPS-Ground/releases/latest)
and install it:

```bash
sudo dpkg -i ultragps-xy_*_amd64.deb
```

This installs `UltraGpsXY` to `/usr/bin` (on your `PATH`) and pulls in its
runtime dependencies automatically.

## Using the build script

`build.sh` is the single entry point for building, packaging, cleaning, and
previewing the docs. Run it from the repository root:

```bash
./build.sh <command> [options]
```

### Commands

| Command | What it does |
|---------|--------------|
| `build` | Compile the `UltraGpsXY` binary into `./build/` |
| `deb` | Build the installable Debian package into `./build/` |
| `clean` | Remove the `./build/` directory (like `make clean`) |
| `docs` | Preview the documentation locally with live reload |
| `docs-build` | Render the static documentation site into `./site/` |

### Options

| Option | Applies to | Effect |
|--------|-----------|--------|
| `-v`, `--verbose` | `build` | Configure with `-DVERBOSE_LOGGING=ON` (debug build, `SPDLOG_*` macros active) |
| `-h`, `--help` | — | Show usage |

### Compiling the program

```bash
./build.sh build            # optimised binary → ./build/UltraGpsXY
./build.sh build --verbose  # debug build with verbose logging compiled in
```

Run it:

```bash
./build/UltraGpsXY          # or just `UltraGpsXY` if installed via the .deb
```

Common options (see the [Usage](usage.md) page or `UltraGpsXY --help` for the
full list):

```bash
./build/UltraGpsXY --serial-port /dev/ttyUSB0 --baud-rate 115200
./build/UltraGpsXY --net-device eth0 --tcp-port 9100 --udp-port 9101 --verbose
```

### Building and installing the Debian package

```bash
./build.sh deb              # → ./build/ultragps-xy_<version>_amd64.deb
sudo dpkg -i build/ultragps-xy_*_amd64.deb
```

This installs the binary to `/usr/bin/UltraGpsXY` (on your `PATH`) and resolves
its runtime dependencies (`libc6`, `libstdc++6`, `libgcc-s1`) automatically.

!!! note
    The `deb` target builds in a temporary directory under `/tmp` rather than in
    place, then copies the finished `.deb` into `./build/`. This isolates the
    transient files `dpkg-shlibdeps` creates while packaging from the working
    tree; plain compilation is unaffected.

### Cleaning

```bash
./build.sh clean            # removes ./build/
```

## Previewing the documentation

The docs are built with [MkDocs](https://www.mkdocs.org/). To preview them
locally without pushing:

```bash
pip install -r docs/requirements.txt    # one-time: install MkDocs
./build.sh docs                         # live preview at http://127.0.0.1:8000
./build.sh docs-build                   # render static site into ./site/
```

Pushing documentation changes to the `release` branch automatically rebuilds
and republishes the site via GitHub Actions.

## Releasing a version

Released versions live in the repository's **Releases** tab, each with the
`.deb` package attached. The `release` branch is the stable snapshot of the most
recent release.

Releases are cut by pushing a **version tag** (`vX.Y`). Pushing the tag triggers
the `Release` workflow, which builds the `.deb` and publishes a GitHub Release
with it attached.

To cut a release:

1. Bump the version in `CMakeLists.txt` so it matches the tag you intend to
   push — `project(UltraGpsXY VERSION X.Y ...)`. The release workflow **fails**
   if the tag and the package version disagree, so this must be done first.
2. Push that commit onto the `release` branch:

```bash
git checkout release
git merge --ff-only <your-dev-branch>
git push origin release
```

3. Tag the release commit and push the tag:

```bash
git tag vX.Y            # e.g. v1.0 — must match project(VERSION) above
git push origin vX.Y
```

The workflow then builds `ultragps-xy_X.Y_amd64.deb` and attaches it to a new
release named "UltraGPS Ground Station vX.Y", with auto-generated notes. Users
can then download and install it:

```bash
sudo dpkg -i ultragps-xy_X.Y_amd64.deb
```
