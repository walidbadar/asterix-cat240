/*
 * SPDX-FileCopyrightText: 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <errno.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>

#include <asterix/cat240.h>

#include "cat240_port.h"

/* Pixel coordinates are stepped along a radial in Q16.16 fixed point. */
#define Q16_ONE  (1 << 16)
#define Q16_HALF (1 << 15)

#define TWO_PI 6.283185307f

static int check_image(const struct asterix_cat240_cartesian_video *image)
{
	if ((image == NULL) || (image->pixels == NULL) || (image->radial == NULL) ||
	    (image->radial_size == 0U)) {
		return -EINVAL;
	}

	if ((image->width == 0U) || (image->height == 0U) || (image->center_x >= image->width) ||
	    (image->center_y >= image->height)) {
		return -EINVAL;
	}

	if ((image->pixel_dur == 0U) || (image->radials == 0U)) {
		return -EINVAL;
	}

	return 0;
}

/*
 * Samples the pixels along the azimuth from the center outward into the
 * scratch buffer and returns their number, or -EMSGSIZE when the buffer is
 * full before the radial leaves the image.
 */
static int sample_radial(const struct asterix_cat240_cartesian_video *image, uint32_t azimuth)
{
	float angle = (float)azimuth * TWO_PI / (float)ASTERIX_CAT240_AZIMUTH_STEPS;
	/* Azimuth 0 points to decreasing rows and turns clockwise. */
	int32_t dx = (int32_t)(sinf(angle) * (float)Q16_ONE);
	int32_t dy = -(int32_t)(cosf(angle) * (float)Q16_ONE);
	int32_t x = ((int32_t)image->center_x << 16) + Q16_HALF;
	int32_t y = ((int32_t)image->center_y << 16) + Q16_HALF;
	int32_t x_end = (int32_t)image->width << 16;
	int32_t y_end = (int32_t)image->height << 16;
	size_t n = 0;

	while ((x >= 0) && (x < x_end) && (y >= 0) && (y < y_end)) {
		if (n == image->radial_size) {
			return -EMSGSIZE;
		}

		image->radial[n] =
			image->pixels[(size_t)(y >> 16) * image->width + (size_t)(x >> 16)];
		n++;
		x += dx;
		y += dy;
	}

	return (int)n;
}

int asterix_cat240_send_cartesian_video(struct asterix_cat240 *ctx,
					const struct asterix_cat240_cartesian_video *image)
{
	struct asterix_cat240_video video;
	int ret;

	if (ctx == NULL) {
		return -EINVAL;
	}

	ret = check_image(image);
	if (ret != 0) {
		return ret;
	}

	video = (struct asterix_cat240_video){
		.start_rg = 0,
		.cell_dur = image->pixel_dur,
		.femto = image->femto,
		.resolution = ASTERIX_CAT240_RES_HIGH,
		.cells = image->radial,
	};

	for (uint32_t i = 0; i < image->radials; i++) {
		uint32_t start_az = i * ASTERIX_CAT240_AZIMUTH_STEPS / image->radials;
		uint32_t end_az = (i + 1U) * ASTERIX_CAT240_AZIMUTH_STEPS / image->radials;

		ret = sample_radial(image, start_az);
		if (ret < 0) {
			return ret;
		}

		video.start_az = (uint16_t)start_az;
		/* The last sector ends at 360 degrees, which wraps to 0. */
		video.end_az = (uint16_t)end_az;
		video.len = (size_t)ret;

		ret = asterix_cat240_send_video(ctx, &video);
		if (ret < 0) {
			return ret;
		}

		if (image->radial_interval_us != 0U) {
			(void)usleep(image->radial_interval_us);
		}
	}

	return (int)image->radials;
}
