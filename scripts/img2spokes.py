#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Muhammad Waleed Badar
# SPDX-License-Identifier: GPL-3.0-only
"""Convert an image into one revolution of radar spokes for cat240-send.

The radar sits at the image centre. Azimuth 0 points up and increases
clockwise. Each spoke walks outward from the centre along its bearing and
takes one pixel per range cell, as an 8 bit amplitude, with zero beyond the
image edge. The output file holds spokes times cells octets, the first spoke
at azimuth 0, which is the replay file format of cat240-send.

Usage:

    scripts/img2spokes.py chart.png chart.spokes --spokes 2048 --cells 1024
    build-host/cat240-send -s 2048 -c 1024 239.192.43.78 chart.spokes

where chart.png is any image of yours, for example a screenshot of a chart.
"""

import argparse
import math
import sys

from PIL import Image

DEFAULT_SPOKES = 2048
DEFAULT_CELLS = 1024
MAX_SPOKES = 65536


def parse_arguments():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("image", help="any image Pillow can open")
    parser.add_argument("output", help="spoke file to write")
    parser.add_argument("--spokes", type=int, default=DEFAULT_SPOKES,
                        help=f"spokes per revolution (default {DEFAULT_SPOKES})")
    parser.add_argument("--cells", type=int, default=DEFAULT_CELLS,
                        help=f"range cells per spoke (default {DEFAULT_CELLS})")
    parser.add_argument("--pixels-per-cell", type=float, default=None,
                        help="range scale; by default the image half width fills the cells")
    parser.add_argument("--invert", action="store_true",
                        help="dark pixels become strong echoes, for maps and charts")
    args = parser.parse_args()

    if not 1 <= args.spokes <= MAX_SPOKES:
        parser.error(f"spokes must be 1 to {MAX_SPOKES}")
    if args.cells < 1:
        parser.error("cells must be at least 1")
    return parser, args


def load_grayscale(parser, path):
    """Open the image as 8 bit grayscale, or exit with a one line error."""
    try:
        return Image.open(path).convert("L")
    except OSError as err:
        parser.error(f"cannot open {path}: {err.strerror or err}")


def image_to_spokes(image, spokes, cells, pixels_per_cell, invert):
    """Sample the image along every spoke and return the spokes as bytes."""
    width, height = image.size
    pixels = image.load()
    centre_x = width / 2.0
    centre_y = height / 2.0

    out = bytearray(spokes * cells)
    for spoke in range(spokes):
        angle = 2.0 * math.pi * spoke / spokes
        step_x = math.sin(angle) * pixels_per_cell
        step_y = -math.cos(angle) * pixels_per_cell   # up the image is north
        first = spoke * cells

        for cell in range(cells):
            x = int(centre_x + cell * step_x)
            y = int(centre_y + cell * step_y)
            if 0 <= x < width and 0 <= y < height:
                value = pixels[x, y]
                out[first + cell] = 255 - value if invert else value

    return out


def main():
    parser, args = parse_arguments()
    image = load_grayscale(parser, args.image)

    width, height = image.size
    pixels_per_cell = args.pixels_per_cell
    if pixels_per_cell is None:
        pixels_per_cell = (min(width, height) / 2.0) / args.cells

    spokes = image_to_spokes(image, args.spokes, args.cells, pixels_per_cell, args.invert)

    with open(args.output, "wb") as f:
        f.write(spokes)

    print(f"{args.output}: {args.spokes} spokes of {args.cells} cells from {width}x{height} "
          f"pixels, {pixels_per_cell:.3f} pixels per cell")
    return 0


if __name__ == "__main__":
    sys.exit(main())
