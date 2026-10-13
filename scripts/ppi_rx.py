#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Muhammad Waleed Badar
# SPDX-License-Identifier: GPL-3.0-only
"""Receive ASTERIX Category 240 video on UDP and paint it as a PPI image.

This is an independent check of a stream. It decodes the data block, the
FSPEC and the data items itself, without the library, then reports how many
messages were well formed, whether the message indices were contiguous, and
writes a PNG of the received video as a radar display would show it.

Usage:

    scripts/ppi_rx.py <port> <output.png> <seconds>

Example, with the tool sending two revolutions to the loopback interface:

    scripts/ppi_rx.py 4378 out.png 6 & sleep 1
    build-host/cat240-send -p 4378 -n 2 127.0.0.1
"""

import math
import socket
import struct
import sys
import time

from PIL import Image

# Field Reference Numbers (FRN) of the standard CAT-240 User Application
# Profile, in the order the items appear in a record.
FRN_DATA_SOURCE = 1       # I240/010: SAC and SIC
FRN_MESSAGE_TYPE = 2      # I240/000
FRN_RECORD_HEADER = 3     # I240/020: message index
FRN_SUMMARY = 4           # I240/030: free text
FRN_HEADER_NANO = 5       # I240/040: azimuths, start range, cell duration in ns
FRN_HEADER_FEMTO = 6      # I240/041: the same, cell duration in fs
FRN_RESOLUTION = 7        # I240/048: compression flag and bits per cell
FRN_COUNTERS = 8          # I240/049: valid octets and valid cells
FRN_BLOCK_LOW = 9         # I240/050: video blocks of 4 octets
FRN_BLOCK_MEDIUM = 10     # I240/051: video blocks of 64 octets
FRN_BLOCK_HIGH = 11       # I240/052: video blocks of 256 octets
FRN_TIME_OF_DAY = 12      # I240/140

VIDEO_BLOCK_SIZES = {FRN_BLOCK_LOW: 4, FRN_BLOCK_MEDIUM: 64, FRN_BLOCK_HIGH: 256}

MESSAGE_VIDEO_SUMMARY = 1
MESSAGE_VIDEO = 2

CATEGORY = 240
AZIMUTH_STEPS = 65536      # azimuth units in a full turn
TIME_OF_DAY_HZ = 128       # time of day ticks per second

IMAGE_SIZE = 512           # pixels, the PPI is a circle inside this square


class MalformedMessage(Exception):
    """The datagram is not a well formed Category 240 data block."""


def read_fspec(buf, pos):
    """Return the set of FRNs present and the position after the FSPEC.

    Each FSPEC octet names seven FRNs in its upper bits; the lowest bit says
    whether another FSPEC octet follows.
    """
    present = set()
    frn = 1
    while True:
        if pos >= len(buf):
            raise MalformedMessage("FSPEC runs past the end")
        octet = buf[pos]
        pos += 1
        for bit in range(7, 0, -1):
            if octet & (1 << bit):
                present.add(frn)
            frn += 1
        if not octet & 1:
            return present, pos


def decode_record(buf):
    """Decode one data block holding one record into a dictionary.

    Raises MalformedMessage when the block header or the layout is wrong.
    """
    if len(buf) < 3 or buf[0] != CATEGORY:
        raise MalformedMessage("not a Category 240 data block")
    block_len = struct.unpack(">H", buf[1:3])[0]
    if block_len != len(buf):
        raise MalformedMessage(f"LEN is {block_len}, datagram holds {len(buf)}")

    present, pos = read_fspec(buf, 3)
    record = {}

    if FRN_DATA_SOURCE in present:
        record["sac"], record["sic"] = buf[pos], buf[pos + 1]
        pos += 2

    if FRN_MESSAGE_TYPE in present:
        record["type"] = buf[pos]
        pos += 1

    if FRN_RECORD_HEADER in present:
        record["index"] = struct.unpack(">I", buf[pos:pos + 4])[0]
        pos += 4

    if FRN_SUMMARY in present:
        length = buf[pos]
        record["summary"] = buf[pos + 1:pos + 1 + length].decode("ascii", "replace")
        pos += 1 + length

    for frn in (FRN_HEADER_NANO, FRN_HEADER_FEMTO):
        if frn in present:
            fields = struct.unpack(">HHII", buf[pos:pos + 12])
            record["start_az"], record["end_az"], record["start_rg"], record["cell_dur"] = fields
            record["femto"] = frn == FRN_HEADER_FEMTO
            pos += 12

    if FRN_RESOLUTION in present:
        record["compressed"] = bool(buf[pos] & 0x80)
        record["resolution"] = buf[pos + 1]
        pos += 2

    if FRN_COUNTERS in present:
        record["valid_octets"] = struct.unpack(">H", buf[pos:pos + 2])[0]
        record["valid_cells"] = int.from_bytes(buf[pos + 2:pos + 5], "big")
        pos += 5

    for frn, block_size in VIDEO_BLOCK_SIZES.items():
        if frn in present:
            repetitions = buf[pos]
            pos += 1
            valid = record.get("valid_octets", repetitions * block_size)
            record["cells"] = buf[pos:pos + valid]
            pos += repetitions * block_size

    if FRN_TIME_OF_DAY in present:
        ticks = int.from_bytes(buf[pos:pos + 3], "big")
        record["time_of_day_s"] = ticks / TIME_OF_DAY_HZ
        pos += 3

    if pos > len(buf):
        raise MalformedMessage("items run past the end")
    return record


def receive(port, seconds):
    """Collect every datagram that arrives on the port for the given time.

    Decoding and painting happen afterwards, so that a slow Python loop does
    not overflow the socket buffer while the sender is running.
    """
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 8 << 20)
    sock.bind(("127.0.0.1", port))
    sock.settimeout(0.5)

    datagrams = []
    deadline = time.time() + seconds
    while time.time() < deadline:
        try:
            datagram, _ = sock.recvfrom(65536)
        except socket.timeout:
            continue
        datagrams.append(datagram)
    return datagrams


def paint(videos, path):
    """Draw each spoke as a ray from the centre and save the picture.

    The longest spoke received sets the range scale, so the picture always
    fills the circle whatever the cell count.
    """
    image = Image.new("L", (IMAGE_SIZE, IMAGE_SIZE))
    pixels = image.load()
    centre = IMAGE_SIZE // 2
    longest = max((len(video["cells"]) for video in videos), default=1)
    pixels_per_cell = (centre - 1) / longest

    for video in videos:
        # Azimuth 0 is up (north) and increases clockwise.
        angle = video["start_az"] * 2 * math.pi / AZIMUTH_STEPS
        step_x = math.sin(angle) * pixels_per_cell
        step_y = -math.cos(angle) * pixels_per_cell
        for cell, amplitude in enumerate(video["cells"]):
            x = int(centre + 0.5 + cell * step_x)
            y = int(centre + 0.5 + cell * step_y)
            if 0 <= x < IMAGE_SIZE and 0 <= y < IMAGE_SIZE:
                pixels[x, y] = amplitude

    image.save(path)


def main():
    if len(sys.argv) != 4:
        print(__doc__)
        return 2
    port = int(sys.argv[1])
    output = sys.argv[2]
    seconds = float(sys.argv[3])

    videos = []
    summary = None
    malformed = 0
    for datagram in receive(port, seconds):
        try:
            record = decode_record(datagram)
        except MalformedMessage:
            malformed += 1
            continue
        if record.get("type") == MESSAGE_VIDEO_SUMMARY:
            summary = record.get("summary")
        elif record.get("type") == MESSAGE_VIDEO:
            videos.append(record)
        else:
            malformed += 1

    paint(videos, output)

    indices = [video["index"] for video in videos]
    contiguous = bool(indices) and indices == list(range(indices[0], indices[0] + len(indices)))
    print(f"summary={summary!r}")
    print(f"video messages={len(videos)} malformed={malformed} "
          f"indices_contiguous={contiguous} first_index={indices[0] if indices else None}")
    print(f"wrote {output}")
    return 0 if malformed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
