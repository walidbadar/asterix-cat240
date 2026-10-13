/*
 * SPDX-FileCopyrightText: 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <math.h>

#include <zephyr/app_version.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <asterix/cat240.h>

LOG_MODULE_REGISTER(main, CONFIG_APP_LOG_LEVEL);

/* Azimuth step between two radials: 1 degree. */
#define AZIMUTH_STEP (ASTERIX_CAT240_AZIMUTH_STEPS / 360U)
/* Cell duration in nanoseconds: 100 ns is a 15 m range cell. */
#define CELL_DUR_NS  100U
/* Log one line per turn of the antenna. */
#define LOG_PERIOD   (ASTERIX_CAT240_AZIMUTH_STEPS / AZIMUTH_STEP)

#if defined(CONFIG_APP_CARTESIAN)
#define IMAGE_SIZE   CONFIG_APP_CARTESIAN_SIZE
#define IMAGE_CENTER (CONFIG_APP_CARTESIAN_SIZE / 2)
/* Pixel size 10 m: 66.713 ns round trip, in femtoseconds. */
#define PIXEL_DUR_FS 66713000U
/* The longest radial runs from the center to a corner. */
#define RADIAL_SIZE  (IMAGE_SIZE + 2)

static uint8_t image_pixels[IMAGE_SIZE * IMAGE_SIZE];
static uint8_t radial_buf[RADIAL_SIZE];

/* Range rings, a crosshair and a filled sector from 30 to 60 degrees. */
static void generate_pattern(void)
{
	for (int y = 0; y < IMAGE_SIZE; y++) {
		for (int x = 0; x < IMAGE_SIZE; x++) {
			int dx = x - IMAGE_CENTER;
			int dy = IMAGE_CENTER - y;
			float range = sqrtf((float)(dx * dx + dy * dy));
			/* Degrees clockwise from north. */
			float azimuth = atan2f((float)dx, (float)dy) * 180.0f / 3.14159265f;
			uint8_t value = 16;

			if (azimuth < 0.0f) {
				azimuth += 360.0f;
			}

			if ((azimuth >= 30.0f) && (azimuth < 60.0f)) {
				value = 96;
			}

			if (((int)range % (IMAGE_SIZE / 8)) == 0) {
				value = 160;
			}

			if ((dx == 0) || (dy == 0)) {
				value = 255;
			}

			image_pixels[y * IMAGE_SIZE + x] = value;
		}
	}
}

static int run(struct asterix_cat240 *stream)
{
	const struct asterix_cat240_cartesian_video image = {
		.pixels = image_pixels,
		.width = IMAGE_SIZE,
		.height = IMAGE_SIZE,
		.center_x = IMAGE_CENTER,
		.center_y = IMAGE_CENTER,
		.pixel_dur = PIXEL_DUR_FS,
		.femto = true,
		.radials = CONFIG_APP_CARTESIAN_RADIALS,
		.radial_interval_us =
			CONFIG_APP_CARTESIAN_TURN_MS * 1000U / CONFIG_APP_CARTESIAN_RADIALS,
		.radial = radial_buf,
		.radial_size = sizeof(radial_buf),
	};
	uint32_t turns = 0;
	int ret;

	generate_pattern();

	while (1) {
		ret = asterix_cat240_send_cartesian_video(stream, &image);
		if (ret < 0) {
			LOG_WRN("Turn %u not sent (%d)", turns, ret);
			k_sleep(K_MSEC(CONFIG_APP_CARTESIAN_TURN_MS));
		} else {
			LOG_INF("Turn %u sent, %d radials of a %u pixel image", turns, ret,
				IMAGE_SIZE);
		}

		turns++;
	}

	return 0;
}
#else
static uint8_t cells[CONFIG_APP_RADIAL_CELLS];

/* Fills the radial with a synthetic echo: a target whose range follows the azimuth. */
static void generate_radial(uint16_t azimuth)
{
	size_t target = (size_t)azimuth * CONFIG_APP_RADIAL_CELLS / ASTERIX_CAT240_AZIMUTH_STEPS;

	for (size_t i = 0; i < CONFIG_APP_RADIAL_CELLS; i++) {
		size_t dist = (i > target) ? (i - target) : (target - i);

		cells[i] = (dist < 8U) ? (uint8_t)(255U - (dist * 32U)) : (uint8_t)(i & 0x0FU);
	}
}

static int run(struct asterix_cat240 *stream)
{
	struct asterix_cat240_video video = {
		.start_rg = 0,
		.cell_dur = CELL_DUR_NS,
		.resolution = ASTERIX_CAT240_RES_HIGH,
		.cells = cells,
		.len = sizeof(cells),
	};
	uint16_t azimuth = 0;
	uint32_t radials = 0;
	int ret;

	while (1) {
		generate_radial(azimuth);
		video.start_az = azimuth;
		video.end_az = azimuth + AZIMUTH_STEP;

		ret = asterix_cat240_send_video(stream, &video);
		if (ret < 0) {
			LOG_WRN("Radial at azimuth %u not sent (%d)", azimuth, ret);
		} else if ((radials % LOG_PERIOD) == 0U) {
			LOG_INF("Radial %u sent, %d octets, %u cells of %u bits", radials, ret,
				CONFIG_APP_RADIAL_CELLS,
				asterix_cat240_bits_per_cell(video.resolution));
		}

		radials++;
		azimuth += AZIMUTH_STEP;

		k_sleep(K_MSEC(CONFIG_APP_RADIAL_PERIOD_MS));
	}

	return 0;
}
#endif /* CONFIG_APP_CARTESIAN */

static struct asterix_cat240 stream;

int main(void)
{
	const struct asterix_cat240_config config = {
		.addr = CONFIG_APP_DEST_ADDR,
		.port = CONFIG_APP_DEST_PORT,
		.iface = CONFIG_APP_IFACE,
		.sac = 1,
		.sic = 1,
	};
	int ret;

	LOG_INF("ASTERIX Category 240 sample %s", APP_VERSION_STRING);

	ret = asterix_cat240_init(&stream, &config);
	if (ret < 0) {
		LOG_ERR("Cannot open the stream to %s:%u (%d)", config.addr, config.port, ret);
		return 0;
	}

	ret = asterix_cat240_send_summary(&stream,
					  "Zephyr ASTERIX CAT240 sample " APP_VERSION_STRING);
	if (ret < 0) {
		LOG_WRN("Summary not sent (%d)", ret);
	}

	return run(&stream);
}
