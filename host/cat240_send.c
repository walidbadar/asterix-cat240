/*
 * SPDX-FileCopyrightText: 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

/*
 * Sends an 8 bit raw image, or a test pattern, as ASTERIX Category 240 radar
 * video, turn after turn.
 */

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <asterix/cat240.h>

#define DEFAULT_PORT     8600
#define DEFAULT_SIZE     256
#define DEFAULT_RADIALS  720
#define DEFAULT_TURN_MS  2000
#define DEFAULT_PIXEL_M  10.0
#define LIGHT_SPEED_M_S  299792458.0
#define FEMTO_PER_SECOND 1e15

static struct asterix_cat240 stream;

static void usage(const char *prog)
{
	fprintf(stderr,
		"Usage: %s [options] <address> [<image.raw> <width> <height>]\n"
		"Sends an 8 bit raw image, or a test pattern without one, as CAT-240 video.\n"
		"  -p <port>     UDP port (default %d)\n"
		"  -i <iface>    network interface to send from (default: routed)\n"
		"  -s <pixels>   test pattern size (default %d)\n"
		"  -r <radials>  radials per turn (default %d)\n"
		"  -t <ms>       turn period in milliseconds (default %d)\n"
		"  -x <meters>   pixel size in meters (default %.0f)\n"
		"  -n <turns>    stop after this many turns (default: run forever)\n"
		"  -S <sac>      System Area Code (default 1)\n"
		"  -I <sic>      System Identification Code (default 1)\n",
		prog, DEFAULT_PORT, DEFAULT_SIZE, DEFAULT_RADIALS, DEFAULT_TURN_MS,
		DEFAULT_PIXEL_M);
}

/* Range rings, a crosshair and a filled sector from 30 to 60 degrees. */
static void generate_pattern(uint8_t *pixels, int size)
{
	int center = size / 2;

	for (int y = 0; y < size; y++) {
		for (int x = 0; x < size; x++) {
			int dx = x - center;
			int dy = center - y;
			double range = sqrt((double)(dx * dx + dy * dy));
			double azimuth = atan2((double)dx, (double)dy) * 180.0 / M_PI;
			uint8_t value = 16;

			if (azimuth < 0.0) {
				azimuth += 360.0;
			}

			if ((azimuth >= 30.0) && (azimuth < 60.0)) {
				value = 96;
			}

			if (((int)range % (size / 8)) == 0) {
				value = 160;
			}

			if ((dx == 0) || (dy == 0)) {
				value = 255;
			}

			pixels[y * size + x] = value;
		}
	}
}

static uint8_t *load_raw(const char *path, size_t len)
{
	uint8_t *pixels = malloc(len);
	FILE *f;
	size_t n;

	if (pixels == NULL) {
		return NULL;
	}

	f = fopen(path, "rb");
	if (f == NULL) {
		fprintf(stderr, "%s: %s\n", path, strerror(errno));
		free(pixels);
		return NULL;
	}

	n = fread(pixels, 1, len, f);
	(void)fclose(f);

	if (n != len) {
		fprintf(stderr, "%s: holds %zu octets, %zu expected\n", path, n, len);
		free(pixels);
		return NULL;
	}

	return pixels;
}

int main(int argc, char **argv)
{
	struct asterix_cat240_config config = {.port = DEFAULT_PORT, .sac = 1, .sic = 1};
	struct asterix_cat240_cartesian_video image = {
		.radials = DEFAULT_RADIALS,
		.femto = true,
	};
	int size = DEFAULT_SIZE;
	long turn_ms = DEFAULT_TURN_MS;
	long turns = -1;
	double pixel_m = DEFAULT_PIXEL_M;
	uint8_t *pixels;
	uint8_t *radial;
	size_t radial_size;
	int opt;
	int ret;

	while ((opt = getopt(argc, argv, "p:i:s:r:t:x:n:S:I:h")) != -1) {
		switch (opt) {
		case 'p':
			config.port = (uint16_t)atoi(optarg);
			break;
		case 'i':
			config.iface = optarg;
			break;
		case 's':
			size = atoi(optarg);
			break;
		case 'r':
			image.radials = (uint16_t)atoi(optarg);
			break;
		case 't':
			turn_ms = atol(optarg);
			break;
		case 'x':
			pixel_m = atof(optarg);
			break;
		case 'n':
			turns = atol(optarg);
			break;
		case 'S':
			config.sac = (uint8_t)atoi(optarg);
			break;
		case 'I':
			config.sic = (uint8_t)atoi(optarg);
			break;
		default:
			usage(argv[0]);
			return EXIT_FAILURE;
		}
	}

	if ((optind >= argc) || ((argc - optind != 1) && (argc - optind != 4))) {
		usage(argv[0]);
		return EXIT_FAILURE;
	}
	config.addr = argv[optind];

	if (argc - optind == 4) {
		image.width = (uint16_t)atoi(argv[optind + 2]);
		image.height = (uint16_t)atoi(argv[optind + 3]);
		if ((image.width == 0U) || (image.height == 0U)) {
			usage(argv[0]);
			return EXIT_FAILURE;
		}
		pixels = load_raw(argv[optind + 1], (size_t)image.width * image.height);
	} else {
		if (size < 2) {
			usage(argv[0]);
			return EXIT_FAILURE;
		}
		image.width = (uint16_t)size;
		image.height = (uint16_t)size;
		pixels = malloc((size_t)size * (size_t)size);
		if (pixels != NULL) {
			generate_pattern(pixels, size);
		}
	}

	if (pixels == NULL) {
		return EXIT_FAILURE;
	}

	radial_size = (size_t)image.width + (size_t)image.height + 2U;
	radial = malloc(radial_size);
	if (radial == NULL) {
		free(pixels);
		return EXIT_FAILURE;
	}

	image.pixels = pixels;
	image.center_x = image.width / 2U;
	image.center_y = image.height / 2U;
	image.pixel_dur = (uint32_t)(2.0 * pixel_m / LIGHT_SPEED_M_S * FEMTO_PER_SECOND);
	image.radial_interval_us = (uint32_t)(turn_ms * 1000L / image.radials);
	image.radial = radial;
	image.radial_size = radial_size;

	ret = asterix_cat240_init(&stream, &config);
	if (ret < 0) {
		fprintf(stderr, "cannot open the stream to %s:%u: %s\n", config.addr, config.port,
			strerror(-ret));
		free(radial);
		free(pixels);
		return EXIT_FAILURE;
	}

	ret = asterix_cat240_send_summary(&stream, "cat240-send");
	if (ret < 0) {
		fprintf(stderr, "summary not sent: %s\n", strerror(-ret));
	}

	printf("Sending %u by %u pixels of %.1f m as %u radials every %ld ms to %s:%u via %s\n",
	       image.width, image.height, pixel_m, image.radials, turn_ms, config.addr, config.port,
	       (config.iface != NULL) ? config.iface : "the routed interface");

	for (long turn = 0; (turns < 0) || (turn < turns); turn++) {
		ret = asterix_cat240_send_cartesian_video(&stream, &image);
		if (ret < 0) {
			fprintf(stderr, "turn %ld not sent: %s\n", turn, strerror(-ret));
			break;
		}
	}

	(void)asterix_cat240_close(&stream);
	free(radial);
	free(pixels);

	return (ret < 0) ? EXIT_FAILURE : EXIT_SUCCESS;
}
