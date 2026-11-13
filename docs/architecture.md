# Architecture

`UltraGpsXY` is a single C++17 executable built with CMake. It is organised
around a `main.cpp` control loop and four modules, plus third-party libraries
that are compiled in statically.

## Components

| Component | Location | Responsibility |
|-----------|----------|----------------|
| Control loop | `main.cpp` | Argument parsing, module setup, banner, main event loop |
| Comms module | `CommsModule/` | Command validation and framing over the serial link; the command reference printed by `help` (`commandHelp.cpp`) |
| Serial module | `SerialModule/` | Low-level POSIX serial (termios) I/O to the Arduino, including switching the baud rate of the open port in place |
| Server module | `ServerModule/` | TCP command socket + UDP continuous data stream, TCP client limit, client list |
| TUI module | `TuiModule/` | Optional notcurses terminal UI (`--tui`): its own render/input thread, shared `TuiState`, and a `ControlQueue` of requests that the control loop executes |

### Third-party libraries

These are vendored as git submodules and **statically linked**, so the shipped
binary has no third-party runtime dependencies:

- [`spdlog`](https://github.com/gabime/spdlog) — logging.
- [`argparse`](https://github.com/p-ranav/argparse) — command-line parsing.
- The `tomlplusplus` submodule is still checked out but no longer used: the
  build does not include it.
- [`notcurses`](https://github.com/dankamongmen/notcurses) (core library only)
  — terminal UI. It is not a submodule: CMake downloads a pinned release at
  build time, patches it for tmux, and links it statically.

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

1. **Runs TUI requests** from the control queue (empty when headless).
2. **Finishes a boot wait**: once the base-station has booted after the port
   was opened or after `B`/`M` rebooted it at another baud rate, it switches
   the host side to that rate in place (no reopen, so no second DTR reset) and
   re-queries the board's settings.
3. **Receives TCP commands** from network clients via the Server module.
   `help` / `?` is answered here with the command reference, to the asking
   client only, and never forwarded.
4. **Validates** each command through the Comms module, discarding anything
   invalid, and drops commands while the base-station is booting.
5. **Dispatches** by command type:
    - *Normal* commands are sent to the Arduino and the response is returned to
      the TCP clients.
    - *Continuous* commands switch the system into streaming mode.
6. **Streams data**: while in continuous mode, serial data from the Arduino is
   pushed out to clients over UDP.

## Logging

Logging uses `spdlog`. The `SPDLOG_*` macros are gated at compile time by
`SPDLOG_ACTIVE_LEVEL`:

- **Default build** — the macros are compiled out entirely.
  Direct calls such as `spdlog::info()` (banner, module-init messages) are
  always present, regardless of compilation debug level.
- **`-DVERBOSE_LOGGING=ON`** — all `SPDLOG_*` macros are active at DEBUG level
  and the build switches to a debug configuration. See
  [Installation](installation.md) and [Usage](usage.md).

## Terminal UI threading

The TUI thread never touches the serial port or `Comms`:

- **Serial taps.** `Serial` calls an RX and a TX callback for every complete
  line. These callbacks feed the "serial link" log, the tick history and the
  jitter windows (the tick lines of the last 10 s) in `TuiState`.
  `Serial::getReceived()` only keeps the latest line, so the taps are the only
  reliable way to see every line.
- **Help.** `help` / `?` typed in the TUI never reaches the control loop: the
  TUI thread writes the command reference into the `TuiState` log itself.
- **Control queue.** Settings changes and commands typed in the TUI become a
  `ControlRequest`. The control loop drains the queue once per iteration and
  runs each request on its own thread, so baud/port reopens never race
  `checkResponse()`.
- **Logging.** While the TUI is active, spdlog's default logger is replaced by
  a callback sink (shown in the log pane) and a file sink
  (`~/.local/state/ultragps/ultragpsxy.log`). Nothing writes to the terminal.
