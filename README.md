# ASTERIX Category 240

Sends ASTERIX Category 240 radar video ([EUROCONTROL-SPEC-0149-240][spec],
edition 1.3) over UDP. Written against POSIX sockets, so it builds both as a
[Zephyr module][modules] and as a static library for Linux.

[spec]: https://www.eurocontrol.int/sites/default/files/content/documents/nm/asterix/20150513-asterix-cat240-v1.3.pdf
[modules]: https://docs.zephyrproject.org/latest/develop/modules.html

## Overview

ASTERIX (All Purpose STructured Eurocontrol suRveillance Information EXchange)
Category 240 is the EUROCONTROL format for radar video: the echo amplitude
sampled in range cells along each radial, with the azimuth and range geometry
needed to place them.

The library opens a UDP stream to one destination, unicast or multicast, IPv4
or IPv6, and tags every message with one data source identifier (SAC/SIC). It
then sends:

- **Video messages**: one radial of range cells per message and per datagram.
  The video block item (I240/050, 051 or 052) is chosen from the radial
  length, padding and cell counters are filled in, and each message gets the
  next sequence number and, optionally, the time of day.
- **Video summary messages**: a text description of the stream.

Transmit buffer size, multicast TTL and time of day stamping are compile time
settings. On Zephyr they are Kconfig options; elsewhere they are defined on the
compiler command line.

The Zephyr sample and the Linux tool (`cat240-send`) behave like the video
processor of a marine navigation radar: the antenna turns at a configured
speed, every spoke goes out as one message when its azimuth comes round, and a
video summary names the radar once a second. The echoes come from a synthetic
scene of sea clutter, a coastline and a few vessels; a product feeds its
receiver's samples in instead. Test suites run on both systems.

## Documentation

Published at <https://walidbadar.github.io/asterix-cat240/>.

- [Protocol](https://walidbadar.github.io/asterix-cat240/protocol.html): the CAT-240 data block, record and items
- [Usage](https://walidbadar.github.io/asterix-cat240/usage.html): configuration, Zephyr and Linux integration
- [API reference](https://walidbadar.github.io/asterix-cat240/doxygen/): generated with Doxygen

The sources are in [doc](doc) and [include/asterix/cat240.h](include/asterix/cat240.h).
The [Documentation workflow](.github/workflows/docs.yml) builds the site on
every push and deploys it from `main`.

## License

GPL-3.0-only, see [LICENSE](LICENSE).
