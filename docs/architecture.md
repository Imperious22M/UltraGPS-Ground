# Architecture

`UltraGpsXY` is a single C++17 executable built with CMake. It is organised
around a `main.cpp` control loop and three modules, plus three third-party
libraries that are compiled in statically.

## Components

| Component | Location | Responsibility |
|-----------|----------|----------------|
| Control loop | `main.cpp` | Argument parsing, module setup, banner, main event loop |
| Comms module | `CommsModule/` | Command validation and framing over the serial link |
| Serial module | `SerialModule/` | Low-level POSIX serial (termios) I/O to the Arduino |
| Server module | `ServerModule/` | TCP command socket + UDP continuous data stream |

### Third-party libraries

These are vendored as git submodules and **statically linked**, so the shipped
binary has no third-party runtime dependencies:

- [`spdlog`](https://github.com/gabime/spdlog) — logging.
- [`tomlplusplus`](https://github.com/marzer/tomlplusplus) — TOML parsing.
- [`argparse`](https://github.com/p-ranav/argparse) — command-line parsing.

## Data flow

```
   Base-station Arduino
          │  (USB serial, e.g. /dev/ttyACM0 @ 38400)
          ▼
   ┌──────────────┐        ┌──────────────┐
   │ SerialModule │◄──────►│ CommsModule  │
   └──────────────┘        └──────┬───────┘
                                  │
                          ┌───────▼────────┐
                          │  main() loop   │
                          └───────┬────────┘
                                  │
                          ┌───────▼────────┐
                          │ ServerModule   │
                          └───────┬────────┘
             TCP commands (9000)  │  UDP stream (9001)
                                  ▼
                            Network clients
```

## Control loop

The loop in `main()` runs continuously and, on each pass:

1. **Receives TCP commands** from network clients via the Server module.
2. **Validates** each command through the Comms module, discarding anything
   invalid.
3. **Dispatches** by command type:
    - *Normal* commands are sent to the Arduino and the response is returned to
      the requesting client over TCP.
    - *Continuous* commands switch the system into streaming mode.
4. **Streams data**: while in continuous mode, serial data from the Arduino is
   pushed out to clients over UDP.

## Logging

Logging uses `spdlog`. The `SPDLOG_*` macros are gated at compile time by
`SPDLOG_ACTIVE_LEVEL`:

- **Default build** — the macros are compiled out entirely (zero overhead).
  Direct calls such as `spdlog::info()` (banner, module-init messages) are
  always present.
- **`-DVERBOSE_LOGGING=ON`** — all `SPDLOG_*` macros are active at DEBUG level
  and the build switches to a debug configuration. See
  [Installation](installation.md) and [Usage](usage.md).
