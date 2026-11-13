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

The third-party libraries (`spdlog`, `argparse`) are included as git submodules
and statically linked, so the installed binary has no third-party runtime
dependencies. (The `tomlplusplus` submodule is still checked out, but the
build no longer uses it.)

## Requirements

- A C++17 compiler (GCC or Clang) and CMake ≥ 3.15
- `git` (the third-party libraries are git submodules)
- `dpkg`/`cpack` — only needed to build the `.deb` package
- Python 3 with `pip` — only needed to build/preview the documentation
- TUI build dependencies: `libunistring-dev libncurses-dev zlib1g-dev`
  (`sudo apt install ...`). notcurses itself comes from the `notcurses`
  submodule, so no network access is needed at build time.

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
sudo dpkg -i ultragps-xy_*_amd64.deb       # plain terminal
sudo dpkg -i ultragps-xy-tmux_*_amd64.deb  # or: TUI inside tmux
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
| `--bash` | `build`, `deb` | Build the TUI for a plain terminal with stock notcurses (`-DULTRAGPS_TUI_TERMINAL=bash`, the default) |
| `--tmux` | `build`, `deb` | Build the TUI for tmux, with the tmux patch applied to notcurses (`-DULTRAGPS_TUI_TERMINAL=tmux`) |
| `--no-tui` | `build`, `deb` | Build without the notcurses terminal UI (`-DULTRAGPS_TUI=OFF`) |
| `-h`, `--help` | — | Show usage |

### Compiling the program

```bash
./build.sh build            # optimised binary → ./build/UltraGpsXY
./build.sh build --verbose  # debug build with verbose logging compiled in
./build.sh build --tmux     # TUI build that also starts inside tmux
```

The TUI is built in by default, in the **bash** variant (stock notcurses, for a
plain terminal). Stock notcurses hangs at startup inside tmux 3.4 or newer, so
use `--tmux` if you run `UltraGpsXY --tui` inside tmux.

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
./build.sh deb --tmux       # → ./build/ultragps-xy-tmux_<version>_amd64.deb
sudo dpkg -i build/ultragps-xy_*_amd64.deb
```

The tmux package is named `ultragps-xy-tmux`. It installs the same
`/usr/bin/UltraGpsXY` and replaces `ultragps-xy` (only one can be installed).

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

## Terminal UI build options

| CMake option | Default | Effect |
|--------------|---------|--------|
| `ULTRAGPS_TUI` | `ON` | Build the `--tui` interface. If the notcurses submodule or the TUI build dependencies are missing, configuring fails with a hint (use `-DULTRAGPS_TUI=OFF` for a headless build) |
| `ULTRAGPS_TUI_TERMINAL` | `bash` | `bash` builds stock notcurses, for a plain terminal. `tmux` applies `notcurses-tmux-patch-cmake/NotcursesTmuxDa1.cmake` so the TUI also starts inside tmux |

### How notcurses is built

notcurses is the `notcurses` git submodule, pinned to the v3.0.17 commit
(`7767278`). During the build CMake:

1. Checks that the submodule is checked out at the commit this repository
   records (the gitlink). A different commit stops the configure step; run
   `git submodule update --init notcurses` to fix it.
2. Copies the submodule into the build directory
   (`build/notcurses-<variant>-<commit>/src`), so the checkout itself is never
   modified.
3. For the `tmux` variant only, runs the patch script from
   `notcurses-tmux-patch-cmake/` on that copy.
4. Builds `notcurses-core-static` and links it into `UltraGpsXY`.

Each variant and commit gets its own copy, so switching between `--bash` and
`--tmux` in the same build directory never reuses a stale tree. To move to a
newer notcurses, check out the new tag in the submodule, commit the updated
gitlink, and check that the patch script still finds its anchor (it fails the
build if not).

!!! warning "Why there is a tmux variant"
    Stock notcurses (tested 3.0.9 and 3.0.17; master has the same input table) hangs inside
    `notcurses_init()` under tmux 3.4 or newer. tmux answers the Primary Device
    Attributes query with `CSI ? 1;2;4 c`, which notcurses' input automaton does
    not recognise. `notcurses-tmux-patch-cmake/NotcursesTmuxDa1.cmake` adds that reply, plus tmux's
    XTSMGRAPHICS error replies, to its table. A copy installed under
    `/usr/local` usually also needs a `libunistring.so` soname that Ubuntu 24.04
    no longer ships, and no package owns it, so `dpkg-shlibdeps` would fail.
    Static linking from the submodule avoids both problems. `build.sh deb`
    refuses to package a binary that links notcurses dynamically.

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
