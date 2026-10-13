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

This also builds ``cat240-send``, which streams a raw 8 bit image or a test
pattern to a viewer, and runs the test suites against the loopback interface.
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

Cartesian images
----------------

``asterix_cat240_send_cartesian_video()`` sends an 8 bit raster image as one
turn of radar video. It resamples the image into radials from a chosen center
pixel outward, azimuth 0 pointing up the image and increasing clockwise, and
sends each radial as ``asterix_cat240_send_video()`` does. One pixel is one
cell, so the pixel size is given as the round trip time of light over it. The
caller provides a scratch buffer long enough for the longest radial, from the
center to the farthest corner. On Zephyr the function is compiled in with
``CONFIG_ASTERIX_CAT240_CARTESIAN``, which needs the sine and cosine functions
of the C library.

.. code-block:: c

   static uint8_t pixels[128 * 128];
   static uint8_t radial[128 + 2];

   const struct asterix_cat240_cartesian_video image = {
           .pixels = pixels,
           .width = 128,
           .height = 128,
           .center_x = 64,
           .center_y = 64,
           .pixel_dur = 66713000, /* 10 m pixels, in femtoseconds */
           .femto = true,
           .radials = 360,
           .radial_interval_us = 5555,
           .radial = radial,
           .radial_size = sizeof(radial),
   };

   asterix_cat240_send_cartesian_video(&stream, &image);

.. figure:: demo.png
   :alt: A photograph sent with cat240-send, shown on the PPI of RadarView-240

   A photograph streamed with ``cat240-send``, as RadarView-240 shows it.
   The image is centred on the PPI and the viewer's range rings run through it,
   since every pixel has become one range cell.

The ``app`` directory holds a sample that generates one radial per period and
sends it to a configurable destination, or, built with ``cartesian.conf``, sends
a Cartesian test pattern turn after turn. The API reference is built with
Doxygen from ``include/asterix/cat240.h``.
