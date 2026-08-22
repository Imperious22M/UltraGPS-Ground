# UltraGPS Ground Station

Ground control program for the St. Mary's University **UltraGPS** positioning system,
developed as a result of graduate research.

`UltraGpsXY` is designed to run on the ground-station host, which is the computer that 
is connected to the base-station Arduino + UltraGPS shield combo through USB. It maintains 
a serial link to the base-station and allows commands to be sent to the base-station from 
the network through a TCP command socket and a UDP data stream. This allows other computers,
from within the same network, to communicate to the base-station without having to be physically
connected to the base-station.

This program must be running in order for the `UltraGPSControl` program to properly. 

## Documentation

- **[Architecture](architecture.md)** — how the program is structured and how
  data flows between the Arduino, the modules, and network clients.
- **[Installation](installation.md)** — building from source or installing the
  Debian package.
- **[Usage](usage.md)** — running the program and its command-line options.

## At a glance

| | |
|---|---|
| Binary | `UltraGpsXY` |
| Language | C++17 (CMake) |
| Serial default | `/dev/ttyACM0` @ 38400 bps |
| Default TCP command port | `9000` |
| Default UDP stream port | `9001` |
| Platform | Linux (packaged for Ubuntu/Debian, amd64) |
