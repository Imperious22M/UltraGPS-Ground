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
  is returned to the client.
- Runs a **UDP data stream** (default port `9001`): while in continuous mode,
  live position data from the Arduino is streamed out to clients.

It is a single C++17 executable. The third-party libraries (`spdlog`,
`tomlplusplus`, `argparse`) are vendored as git submodules and statically
linked, so the installed binary has no third-party runtime dependencies.

See the [full documentation](https://imperious22m.github.io/UltraGPS-Ground/)
for architecture, usage, and design notes.

## Requirements

- A C++17 compiler (GCC or Clang) and CMake ≥ 3.15
- `git` (the third-party libraries are git submodules)
- `dpkg`/`cpack` — only needed to build the `.deb` package

Clone with submodules:

```bash
git clone --recurse-submodules https://github.com/Imperious22M/UltraGPS-Ground.git
cd UltraGPS-Ground
# If you already cloned without --recurse-submodules:
git submodule update --init --recursive
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

### Building and installing the Debian package

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

### Releasing a version

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
2. Get that commit onto the `release` branch (it is the stable snapshot):

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

### Previewing the documentation

The docs are built with [MkDocs](https://www.mkdocs.org/). To preview them
locally without pushing:

```bash
pip install -r docs/requirements.txt    # one-time: install MkDocs
./build.sh docs                         # live preview at http://127.0.0.1:8000
./build.sh docs-build                   # render static site into ./site/
```

Pushing documentation changes to the `release` branch automatically rebuilds
and republishes the site via GitHub Actions.
