/*
 * SPDX-FileCopyrightText: 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

/*
 * Marine radar video source for Linux. Streams a synthetic scene, or replays
 * one recorded revolution of spokes, as ASTERIX Category 240 video with the
 * timing of a rotating antenna, and names the radar in a video summary once a
 * second.
 */

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <asterix/cat240.h>

#include "scene.h"

#define DEFAULT_PORT      4378
#define DEFAULT_RPM       24
#define DEFAULT_SPOKES    2048
#define DEFAULT_CELLS     1024
#define DEFAULT_CELL_NS   100
#define DEFAULT_NAME      "cat240-send X-band radar"
#define SUMMARY_PERIOD_US 1000000ULL
#define USEC_PER_MINUTE   60000000ULL
#define NSEC_PER_USEC     1000ULL
#define USEC_PER_SEC      1000000ULL
#define SUMMARY_MAX       128

static void usage(const char *prog)
{
	fprintf(stderr,
		"Usage: %s [options] <address> [<spokes.raw>]\n"
		"Sends a synthetic marine radar scene, or replays recorded spokes, as CAT-240 "
		"video.\n"
		"  -p <port>     UDP port (default %d)\n"
		"  -i <iface>    network interface to send from (default: routed)\n"
		"  -S <sac>      System Area Code (default 1)\n"
		"  -I <sic>      System Identification Code (default 1)\n"
		"  -N <name>     radar name for the video summary (default \"%s\")\n"
		"  -R <rpm>      antenna speed in revolutions per minute (default %d)\n"
		"  -s <spokes>   spokes per revolution (default %d)\n"
		"  -c <cells>    range cells per spoke (default %d)\n"
		"  -d <ns>       cell duration in nanoseconds (default %d, a %d m cell)\n"
		"  -n <revs>     stop after this many revolutions (default: run forever)\n"
		"<spokes.raw> holds one revolution of 8 bit video, spokes times cells octets,\n"
		"replayed in a loop in place of the synthetic scene.\n",
		prog, DEFAULT_PORT, DEFAULT_NAME, DEFAULT_RPM, DEFAULT_SPOKES, DEFAULT_CELLS,
		DEFAULT_CELL_NS, DEFAULT_CELL_NS * 3 / 20);
}

static uint8_t *load_spokes(const char *path, size_t len)
{
	uint8_t *spokes = malloc(len);
	FILE *f;
	size_t n;

	if (spokes == NULL) {
		return NULL;
	}

	f = fopen(path, "rb");
	if (f == NULL) {
		fprintf(stderr, "%s: %s\n", path, strerror(errno));
		free(spokes);
		return NULL;
	}

	n = fread(spokes, 1, len, f);
	(void)fclose(f);

	if (n != len) {
		fprintf(stderr, "%s: holds %zu octets, %zu expected\n", path, n, len);
		free(spokes);
		return NULL;
	}

	return spokes;
}

static uint64_t monotonic_us(void)
{
	struct timespec ts;

	(void)clock_gettime(CLOCK_MONOTONIC, &ts);

	return ((uint64_t)ts.tv_sec * USEC_PER_SEC) + ((uint64_t)ts.tv_nsec / NSEC_PER_USEC);
}

int main(int argc, char **argv)
{
	struct asterix_cat240_config config = {.port = DEFAULT_PORT, .sac = 1, .sic = 1};
	struct asterix_cat240 stream;
	struct asterix_cat240_video video = {.resolution = ASTERIX_CAT240_RES_HIGH};
	struct scene scene;
	const char *name = DEFAULT_NAME;
	const char *replay_path = NULL;
	uint8_t *replay = NULL;
	uint8_t *cells;
	char summary[SUMMARY_MAX];
	unsigned long rpm = DEFAULT_RPM;
	unsigned long spokes = DEFAULT_SPOKES;
	unsigned long ncells = DEFAULT_CELLS;
	unsigned long cell_ns = DEFAULT_CELL_NS;
	long revolutions = -1;
	uint64_t rev_us;
	uint64_t spoke_us;
	uint64_t start_us;
	uint64_t next_summary_us = 0;
	uint64_t sent = 0;
	uint64_t dropped = 0;
	uint64_t errors = 0;
	bool done = false;
	int opt;
	int ret;

	/* Revolution lines reach a log file as they happen, not in 4 KiB chunks. */
	(void)setvbuf(stdout, NULL, _IOLBF, 0);

	while ((opt = getopt(argc, argv, "p:i:S:I:N:R:s:c:d:n:h")) != -1) {
		switch (opt) {
		case 'p':
			config.port = (uint16_t)atoi(optarg);
			break;
		case 'i':
			config.iface = optarg;
			break;
		case 'S':
			config.sac = (uint8_t)atoi(optarg);
			break;
		case 'I':
			config.sic = (uint8_t)atoi(optarg);
			break;
		case 'N':
			name = optarg;
			break;
		case 'R':
			rpm = strtoul(optarg, NULL, 10);
			break;
		case 's':
			spokes = strtoul(optarg, NULL, 10);
			break;
		case 'c':
			ncells = strtoul(optarg, NULL, 10);
			break;
		case 'd':
			cell_ns = strtoul(optarg, NULL, 10);
			break;
		case 'n':
			revolutions = atol(optarg);
			break;
		default:
			usage(argv[0]);
			return EXIT_FAILURE;
		}
	}

	if ((optind >= argc) || ((argc - optind) > 2) || (rpm == 0UL) || (spokes == 0UL) ||
	    (spokes > 65536UL) || (ncells == 0UL) || (ncells > 65024UL) || (cell_ns == 0UL)) {
		usage(argv[0]);
		return EXIT_FAILURE;
	}
	config.addr = argv[optind];
	if ((argc - optind) == 2) {
		replay_path = argv[optind + 1];
	}

	cells = malloc(ncells);
	if (cells == NULL) {
		return EXIT_FAILURE;
	}

	if (replay_path != NULL) {
		replay = load_spokes(replay_path, (size_t)spokes * ncells);
		if (replay == NULL) {
			free(cells);
			return EXIT_FAILURE;
		}
	}

	scene.cell_m = (uint32_t)((cell_ns * 3UL + 10UL) / 20UL);
	video.start_rg = 0;
	video.cell_dur = (uint32_t)cell_ns;
	video.cells = cells;
	video.len = ncells;

	rev_us = USEC_PER_MINUTE / rpm;
	spoke_us = rev_us / spokes;

	ret = asterix_cat240_init(&stream, &config);
	if (ret < 0) {
		fprintf(stderr, "cannot open the stream to %s:%u: %s\n", config.addr, config.port,
			strerror(-ret));
		free(replay);
		free(cells);
		return EXIT_FAILURE;
	}

	snprintf(summary, sizeof(summary), "%s: %lu rpm, %lu spokes, %lu cells of %u m", name, rpm,
		 spokes, ncells, scene.cell_m);

	printf("%s to %s:%u via %s%s%s\n", summary, config.addr, config.port,
	       (config.iface != NULL) ? config.iface : "the routed interface",
	       (replay_path != NULL) ? ", replaying " : "",
	       (replay_path != NULL) ? replay_path : "");

	start_us = monotonic_us();

	while (!done) {
		uint64_t now_us = monotonic_us() - start_us;
		/* Spokes the antenna has swept so far. */
		uint64_t due = (now_us * spokes) / rev_us;

		/* The antenna does not wait: a full revolution behind, skip to the present. */
		if (due > (sent + spokes)) {
			dropped += due - sent;
			sent = due;
		}

		while ((sent < due) && !done) {
			uint32_t spoke = (uint32_t)(sent % spokes);
			uint32_t revolution = (uint32_t)(sent / spokes);
			uint32_t start_az =
				(uint32_t)(((uint64_t)spoke * ASTERIX_CAT240_AZIMUTH_STEPS) /
					   spokes);
			uint32_t end_az =
				(uint32_t)(((uint64_t)(spoke + 1U) * ASTERIX_CAT240_AZIMUTH_STEPS) /
					   spokes);

			if ((revolutions >= 0) && (revolution >= (uint32_t)revolutions)) {
				done = true;
				break;
			}

			if (replay != NULL) {
				memcpy(cells, &replay[(size_t)spoke * ncells], ncells);
			} else {
				scene.revolution = revolution;
				scene_radial(&scene, (uint16_t)start_az, cells, ncells);
			}

			video.start_az = (uint16_t)start_az;
			video.end_az = (uint16_t)end_az;

			ret = asterix_cat240_send_video(&stream, &video);
			if (ret < 0) {
				errors++;
				if (errors == 1U) {
					fprintf(stderr, "spoke not sent: %s\n", strerror(-ret));
				}
			}

			sent++;

			if (spoke == (spokes - 1U)) {
				printf("revolution %u: %lu spokes, %llu dropped, %llu send "
				       "errors\n",
				       revolution, spokes, (unsigned long long)dropped,
				       (unsigned long long)errors);
			}
		}

		if (!done && (now_us >= next_summary_us)) {
			ret = asterix_cat240_send_summary(&stream, summary);
			if (ret < 0) {
				fprintf(stderr, "summary not sent: %s\n", strerror(-ret));
			}
			next_summary_us += SUMMARY_PERIOD_US;
		}

		if (!done) {
			(void)usleep((useconds_t)spoke_us);
		}
	}

	(void)asterix_cat240_close(&stream);
	free(replay);
	free(cells);

	return (errors == 0U) ? EXIT_SUCCESS : EXIT_FAILURE;
}
