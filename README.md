# UltraGPS Ground Station

Control program for the St. Mary's University **UltraGPS** positioning system,
developed as part of graduate research.

## What it does

`UltraGpsXY` runs on the ground-station host and bridges the positioning
hardware to the network. It:

- Maintains a **serial link** to the base-station Arduino (default
  `/dev/ttyACM0` @ 38400 bps) via the Comms/Serial modules.
- Runs a **TCP command server** (default port `9000`): network clients send
  commands, which are validated and forwarded to the Arduino, and the response
  is returned to the client. `help` (or `?`) returns the command reference,
  which `UltraGpsXY --help` also prints.
- Runs a **UDP data stream** (default port `9001`): while in continuous mode,
  live position data from the Arduino is streamed out to clients.
- Optionally runs an interactive **terminal UI** (`UltraGpsXY --tui`, built
  with [notcurses](https://github.com/dankamongmen/notcurses)). It shows live
  receiver ticks with each receiver's jitter, a 60 s graph per receiver, a log
  of every serial line, the server status and connected clients, a settings
  screen (baud/port reopen, client limit, base-station commands) and a help
  screen with the command reference. It works inside tmux.

It is a single C++17 executable. The third-party libraries (`spdlog`,
`argparse`) are vendored as git submodules and statically linked (the
`tomlplusplus` submodule is still checked out but no longer used). notcurses-core is downloaded at build time, patched for tmux and
statically linked. The installed binary only needs distro libraries (`libc6`,
`libstdc++6`, `libtinfo6`, `zlib1g`).

An Arduino Uno running the ground station Arduino program should be plugged in to a USB 
port of the PC running this program before this program is initiated.

See the [full documentation](https://imperious22m.github.io/UltraGPS-Ground/)
for architecture, usage, and design notes.

## Requirements

- A C++17 compiler (GCC or Clang) and CMake ≥ 3.15
- `git` (the third-party libraries are git submodules)
- `dpkg`/`cpack` — only needed to build the `.deb` package
- For the TUI: `pkg-config libunistring-dev libncurses-dev zlib1g-dev`, plus
  network access on the first build to download notcurses (use `--no-tui` to
  skip it)

Clone with submodules:

```bash
git clone --recurse-submodules https://github.com/Imperious22M/UltraGPS-Ground.git
cd UltraGPS-Ground
# If you already cloned without --recurse-submodules:
git submodule update --init --recursive
```

## Quick start

You don't need to build anything to run the program — each release ships a
prebuilt Debian package. Download the latest `.deb` from the
**[Releases page](https://github.com/Imperious22M/UltraGPS-Ground/releases/latest)**
and install it:

```bash
sudo dpkg -i ultragps-xy_*_amd64.deb
```

This installs `UltraGpsXY` to `/usr/bin` (on your `PATH`) and pulls in its
runtime dependencies automatically. Then run it:

```bash
UltraGpsXY                 # see `UltraGpsXY --help` for options
```

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
| `--no-tui` | `build`, `deb` | Build without the notcurses terminal UI (`-DULTRAGPS_TUI=OFF`) |
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

Common options (see the docs or `UltraGpsXY --help` for the full list):

```bash
./build/UltraGpsXY --serial-port /dev/ttyUSB0 --baud-rate 115200
./build/UltraGpsXY --net-device eth0 --tcp-port 9100 --udp-port 9101 --verbose
```

### Building and installing the Debian package (from a cloned repository)

```bash
./build.sh deb              # → ./build/ultragps-xy_<version>_amd64.deb
sudo dpkg -i build/ultragps-xy_*_amd64.deb
```

This installs the binary to `/usr/bin/UltraGpsXY` (on your `PATH`) and resolves
its runtime dependencies (`libc6`, `libstdc++6`, `libgcc-s1`) automatically.

> **Note** The `deb` target builds in a temporary
> directory under `/tmp` rather than in place.

### Cleaning

```bash
./build.sh clean            # removes ./build/
```

## Developer's Guide

See the **[Developer's Guide](https://imperious22m.github.io/UltraGPS-Ground/developer-guide/)** in the documentation for more details.
