# Usage

Run the program directly (if installed via the `.deb`, `UltraGpsXY` is on your
`PATH`; otherwise use `./build/UltraGpsXY`):

```bash
UltraGpsXY [options]
```

On startup it prints a banner summarising the resolved serial port, baud rate,
IP address, and ports, then initialises the Comms module (serial link) and
starts the Server module.

## Command-line options

All configuration is done through flags — there is no configuration file read at
runtime.

| Flag | Argument | Default | Description |
|------|----------|---------|-------------|
| `-v`, `--verbose` | — | off | Enable verbose (debug-level) logging at runtime |
| `-s`, `--serial-port` | path | `/dev/ttyACM0` | Serial port for the base-station Arduino |
| `-b`, `--baud-rate` | uint | `38400` | Baud rate; one of `9600`, `19200`, `38400`, `57600`, `115200` |
| `-n`, `--net-device` | name | *(auto)* | Network interface to bind for the server (default: active network) |
| `-t`, `--tcp-port` | uint | `9000` | TCP port for the command server |
| `-u`, `--udp-port` | uint | `9001` | UDP port for the continuous data stream |
| `-T`, `--server-timeout` | uint (ms) | `5000` | Server timeout in milliseconds |

## Examples

Run with defaults:

```bash
UltraGpsXY
```

Custom serial port and baud rate:

```bash
UltraGpsXY --serial-port /dev/ttyUSB0 --baud-rate 115200
```

Bind the server to a specific interface and ports, with verbose logging:

```bash
UltraGpsXY --net-device eth0 --tcp-port 9100 --udp-port 9101 --verbose
```

!!! note "Runtime verbose vs. compile-time verbose"
    `--verbose` raises the **runtime** log level to debug for direct `spdlog`
    calls. The `SPDLOG_*` macros (fine-grained loop tracing) are separate and
    are only compiled in when the binary is built with `-DVERBOSE_LOGGING=ON`
    (see [Installation](installation.md)).
