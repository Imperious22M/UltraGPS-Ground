# UltraGPS Ground Station

Control program for the St. Mary's University **UltraGPS** positioning system,
developed as part of graduate research.

`UltraGpsXY` runs on the ground-station host. It maintains a serial link to the
base-station Arduino and exposes the positioning system to the network through a
TCP command socket and a UDP data stream.

## Documentation

- **[Architecture](architecture.md)** — how the program is structured and how
  data flows between the Arduino, the modules, and network clients.
- **[Installation](installation.md)** — building from source or installing the
  Debian package.
- **[Usage](usage.md)** — running the program and its command-line options.
- **[Notes](notes.md)** — design notes, gotchas, and open items.

## At a glance

| | |
|---|---|
| Binary | `UltraGpsXY` |
| Language | C++17 (CMake) |
| Serial default | `/dev/ttyACM0` @ 38400 bps |
| TCP command port | `9000` |
| UDP stream port | `9001` |
| Platform | Linux (packaged for Ubuntu/Debian, amd64) |
