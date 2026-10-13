Usage
=====

The library is written against the POSIX socket, clock and sleep functions
and builds for Zephyr and for Linux or any other POSIX system from the same
sources. A stream is bound to one destination and one data source identifier;
each radial is sent as one video message in one UDP datagram. The optional
``iface`` field of the configuration names the network interface the datagrams
leave through; without it the system routes them.

On Zephyr, enable it with ``CONFIG_ASTERIX_CAT240=y``; it needs UDP
(``CONFIG_NET_UDP``) and the POSIX networking and timer options, which
``CONFIG_POSIX_API=y`` provides. On
Linux, build the static library with the CMake project in ``host``:

.. code-block:: shell

   cmake -S host -B build-host
   cmake --build build-host
   ctest --test-dir build-host

This also builds ``cat240-send``, a marine radar video source for the host,
and runs the test suites against the loopback interface.
Include ``asterix/cat240.h`` on both systems. The build time settings
``ASTERIX_CAT240_TX_BUF_SIZE``, ``ASTERIX_CAT240_MCAST_TTL`` and
``ASTERIX_CAT240_TIME_OF_DAY`` follow the Kconfig options on Zephyr and can be
defined on the compiler command line elsewhere.

.. code-block:: c

   static struct asterix_cat240 stream;
   static uint8_t cells[256];

   const struct asterix_cat240_config config = {
           .addr = "239.192.0.240",
           .port = 8600,
           .sac = 1,
           .sic = 1,
   };
   struct asterix_cat240_video video = {
           .start_az = azimuth,
           .end_az = azimuth + 1,
           .start_rg = 0,
           .cell_dur = 100,
           .resolution = ASTERIX_CAT240_RES_HIGH,
           .cells = cells,
           .len = sizeof(cells),
   };

   asterix_cat240_init(&stream, &config);
   asterix_cat240_send_summary(&stream, "PSR raw video");
   asterix_cat240_send_video(&stream, &video);
   asterix_cat240_close(&stream);

``asterix_cat240_send_video()`` picks the video block item from the radial
length, pads the last block, counts the valid octets and cells and numbers the
messages. With ``CONFIG_ASTERIX_CAT240_TIME_OF_DAY`` every message also carries
the time of day taken from the system real time clock. The transmit buffer
size, ``ASTERIX_CAT240_TX_BUF_SIZE``, bounds the radial length; the default
fits one Ethernet frame.

Errors are reported as negative errno values: ``-EINVAL`` for an invalid
argument or an unopened stream, ``-EMSGSIZE`` for a radial that does not fit,
``-EAFNOSUPPORT`` for a destination of an address family that is not enabled,
and the errno of the socket API when a datagram cannot be sent.

Marine radar sample
-------------------

The ``app`` directory holds a Zephyr application, and ``host`` the equivalent
``cat240-send`` tool for Linux, that behave like the video processor of a
marine navigation radar:

- The antenna turns at ``CONFIG_APP_ANTENNA_RPM`` and sweeps
  ``CONFIG_APP_SPOKES_PER_REV`` spokes per revolution. The azimuth of each
  spoke follows the clock, as it would follow the shaft encoder of a real
  antenna, so a slow network or processor delays spokes but never shifts
  them; falling a whole revolution behind skips to the present and counts the
  spokes dropped. At 24 rpm and 2048 spokes that is about 820 messages per
  second.
- Each spoke carries ``CONFIG_APP_RANGE_CELLS`` cells of 8 bits with a cell
  duration of ``CONFIG_APP_CELL_DUR_NS``: 100 ns is a 15 m cell, and 1024 of
  them reach 15.4 km. A range scale change alters these two values, not the
  message format.
- A video summary with ``CONFIG_APP_RADAR_NAME`` and the settings goes out
  every ``CONFIG_APP_SUMMARY_PERIOD_MS``, so a display that joins late knows
  what it receives.
- The destination is a multicast group on the radar LAN by default,
  ``CONFIG_APP_DEST_ADDR`` and ``CONFIG_APP_DEST_PORT``, and the stream is
  identified by ``CONFIG_APP_SAC`` and ``CONFIG_APP_SIC``.

The echoes come from a synthetic scene in ``app/src/scene.c``, shared by both
programs and written in integer arithmetic: receiver noise, sea clutter that
fades out at 2.5 km and changes every revolution, a coastline with bays and
headlands to the north, a tanker, a fishing boat, a channel buoy and a ferry
closing at 20 knots. A product replaces ``scene_radial()`` with the samples of
its receiver and keeps the rest.

On native_sim/native/64 the sample uses the Native Simulator offloaded sockets
driver, configured in ``app/boards``, so it sends through the host's sockets
without a TAP interface:

.. code-block:: shell

   west build -b native_sim/native/64 app
   ./build/zephyr/zephyr.exe

``cat240-send`` takes the same parameters as options and, given a file, replays
one recorded revolution of spokes in a loop instead of the scene. The file holds
spokes times cells octets of 8 bit video, the first spoke at azimuth 0:

.. code-block:: shell

   ./build-host/cat240-send 239.192.43.78
   ./build-host/cat240-send -R 48 -s 4096 -c 512 -d 50 239.192.43.78
   ./build-host/cat240-send -s 2048 -c 1024 239.192.43.78 revolution.raw

Watch either stream with Wireshark, display filter ``asterix``, or with
``tshark -i any -f "udp port 4378" -d udp.port==4378,asterix``. Two Python
helpers in ``scripts`` need only Pillow: ``ppi_rx.py`` receives a stream,
decodes it independently of the library and paints a PPI image, and
``img2spokes.py`` turns any image into a replay file for ``cat240-send``, so a
chart or a photograph can serve as test video. The API reference is built with
Doxygen from ``include/asterix/cat240.h``.
