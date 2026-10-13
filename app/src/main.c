/*
 * SPDX-FileCopyrightText: 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

/*
 * Marine radar video source. Behaves like the video processor of a
 * navigation radar: the antenna turns at a fixed speed, every spoke is sent
 * as one video message the moment its azimuth comes round, and a video
 * summary names the radar once a second. The echoes come from a synthetic
 * scene; a product replaces scene_radial() with its receiver's samples.
 */

#include <stdbool.h>

#include <zephyr/app_version.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>

#include <asterix/cat240.h>

#include "scene.h"

LOG_MODULE_REGISTER(main, CONFIG_APP_LOG_LEVEL);

#define SPOKES          ((uint32_t)CONFIG_APP_SPOKES_PER_REV)
#define USEC_PER_MINUTE 60000000ULL
/* One antenna revolution and one spoke, in microseconds. */
#define REV_US          (USEC_PER_MINUTE / CONFIG_APP_ANTENNA_RPM)
#define SPOKE_US        (REV_US / SPOKES)
/* Range cell depth: c * cell_dur / 2 is 0.15 m per nanosecond. */
#define CELL_M          ((CONFIG_APP_CELL_DUR_NS * 3U + 10U) / 20U)

#define SUMMARY_MAX 128

static void send_summary(struct asterix_cat240 *stream, const char *text)
{
	int ret = asterix_cat240_send_summary(stream, text);

	if (ret < 0) {
		LOG_WRN("Summary not sent (%d)", ret);
	}
}

static int run(struct asterix_cat240 *stream)
{
	uint8_t cells[CONFIG_APP_RANGE_CELLS];
	char summary[SUMMARY_MAX];
	struct scene scene = {.cell_m = CELL_M};
	struct asterix_cat240_video video = {
		.start_rg = 0,
		.cell_dur = CONFIG_APP_CELL_DUR_NS,
		.resolution = ASTERIX_CAT240_RES_HIGH,
		.cells = cells,
		.len = sizeof(cells),
	};
	int64_t start = k_uptime_ticks();
	uint64_t sent = 0;
	uint64_t next_summary_us = 0;
	uint32_t dropped = 0;
	uint32_t errors = 0;

	snprintk(summary, sizeof(summary), "%s: %u rpm, %u spokes, %u cells of %u m",
		 CONFIG_APP_RADAR_NAME, CONFIG_APP_ANTENNA_RPM, SPOKES, CONFIG_APP_RANGE_CELLS,
		 CELL_M);

	while (true) {
		uint64_t now_us = k_ticks_to_us_floor64(k_uptime_ticks() - start);
		/* Spokes the antenna has swept so far. */
		uint64_t due = (now_us * SPOKES) / REV_US;

		/* The antenna does not wait: a full revolution behind, skip to the present. */
		if (due > (sent + SPOKES)) {
			dropped += (uint32_t)(due - sent);
			sent = due;
		}

		while (sent < due) {
			uint32_t spoke = (uint32_t)(sent % SPOKES);
			uint32_t revolution = (uint32_t)(sent / SPOKES);
			int ret;

			scene.revolution = revolution;
			scene_radial(&scene,
				     (uint16_t)((spoke * ASTERIX_CAT240_AZIMUTH_STEPS) / SPOKES),
				     cells, sizeof(cells));

			video.start_az =
				(uint16_t)((spoke * ASTERIX_CAT240_AZIMUTH_STEPS) / SPOKES);
			/* The last spoke ends at 360 degrees, which wraps to 0. */
			video.end_az =
				(uint16_t)(((spoke + 1U) * ASTERIX_CAT240_AZIMUTH_STEPS) / SPOKES);

			ret = asterix_cat240_send_video(stream, &video);
			if (ret < 0) {
				errors++;
				LOG_DBG("Spoke %u not sent (%d)", spoke, ret);
			}

			sent++;

			if (spoke == (SPOKES - 1U)) {
				LOG_INF("Revolution %u: %u spokes, %u dropped, %u send errors",
					revolution, SPOKES, dropped, errors);
			}
		}

		if (now_us >= next_summary_us) {
			send_summary(stream, summary);
			next_summary_us += (uint64_t)CONFIG_APP_SUMMARY_PERIOD_MS * 1000U;
		}

		k_usleep((int32_t)SPOKE_US);
	}

	return 0;
}

int main(void)
{
	struct asterix_cat240 stream;
	const struct asterix_cat240_config config = {
		.addr = CONFIG_APP_DEST_ADDR,
		.port = CONFIG_APP_DEST_PORT,
		.iface = CONFIG_APP_IFACE,
		.sac = CONFIG_APP_SAC,
		.sic = CONFIG_APP_SIC,
	};
	int ret;

	LOG_INF("ASTERIX Category 240 radar video source %s", APP_VERSION_STRING);
	LOG_INF("%u rpm, %u spokes per revolution, %u cells of %u m", CONFIG_APP_ANTENNA_RPM,
		SPOKES, CONFIG_APP_RANGE_CELLS, CELL_M);

	ret = asterix_cat240_init(&stream, &config);
	if (ret < 0) {
		LOG_ERR("Cannot open the stream to %s:%u (%d)", config.addr, config.port, ret);
		return 0;
	}

	return run(&stream);
}
