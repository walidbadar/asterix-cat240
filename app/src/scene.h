/*
 * SPDX-FileCopyrightText: 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

/*
 * Synthetic marine radar scene: sea clutter around own ship, a coastline
 * to the north and a few vessels, one of them approaching. Integer
 * arithmetic only, so it runs on any Cortex-M without a floating point unit.
 */

#ifndef APP_SCENE_H_
#define APP_SCENE_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct scene {
	/** Depth of one range cell in metres. */
	uint32_t cell_m;
	/** Completed antenna revolutions; moves the vessels and churns the sea. */
	uint32_t revolution;
};

/**
 * @brief Fill one spoke of 8 bit video cells
 *
 * @param scene Scene parameters.
 * @param azimuth Spoke azimuth in 1/65536 of a turn, clockwise from north.
 * @param cells Output cells, the first one at own ship.
 * @param n Number of cells.
 */
void scene_radial(const struct scene *scene, uint16_t azimuth, uint8_t *cells, size_t n);

#ifdef __cplusplus
}
#endif

#endif /* APP_SCENE_H_ */
