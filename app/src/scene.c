/*
 * SPDX-FileCopyrightText: 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "scene.h"

#define AZ_FULL    65536U
#define AZ_HALF    32768U
#define AZ_DEG(d)  ((uint32_t)(d) * AZ_FULL / 360U)
/* Azimuth units per radian, 65536 / (2 * pi). */
#define AZ_PER_RAD 10430U

/* Sea clutter reaches this far from own ship. */
#define CLUTTER_M    2500U
#define CLUTTER_PEAK 140U

/* Land returns fade from this amplitude with distance behind the coast. */
#define LAND_PEAK     230U
#define LAND_FADE_MIN 90U
#define LAND_FADE_M   80U

#define TARGET_PEAK 255U
#define TARGET_EDGE 160U
#define NO_LAND     UINT32_MAX

/* A vessel or a buoy, as the radar sees it from own ship. */
struct target {
	uint32_t bearing;   /* azimuth units */
	uint32_t range_m;   /* at revolution 0 */
	uint32_t length_m;  /* extent across the beam */
	uint32_t depth_m;   /* extent in range */
	uint32_t closing_m; /* range lost per revolution */
	uint32_t closest_m; /* where an approaching vessel turns back */
};

/* Cheap integer hash for noise, clutter speckle and land texture. */
static uint32_t hash(uint32_t key)
{
	uint32_t x = key;

	x ^= x >> 16;
	x *= 0x7feb352dU;
	x ^= x >> 15;
	x *= 0x846ca68bU;
	x ^= x >> 16;

	return x;
}

/* Triangle wave over the azimuth, 0 to 1024 and back once per turn of x. */
static uint32_t triangle(uint32_t phase)
{
	uint32_t x = phase & (AZ_FULL - 1U);

	return (x < AZ_HALF) ? (x / 32U) : ((AZ_FULL - x) / 32U);
}

static uint32_t max_u32(uint32_t a, uint32_t b)
{
	return (a > b) ? a : b;
}

/* Signed azimuth difference, -32768 to 32767. */
static int32_t azimuth_diff(uint32_t a, uint32_t b)
{
	uint32_t wrapped = (a - b + AZ_HALF) & (AZ_FULL - 1U);

	return (int32_t)wrapped - (int32_t)AZ_HALF;
}

/* Range of the coastline on this bearing, or NO_LAND on open sea. */
static uint32_t coast_range_m(uint16_t azimuth)
{
	/* Distance from north, 0 to 180 degrees. */
	uint32_t from_north = (azimuth < AZ_HALF) ? azimuth : (AZ_FULL - azimuth);
	uint32_t range_m;

	if (from_north > AZ_DEG(70)) {
		range_m = NO_LAND;
	} else {
		/* Headlands and bays from two triangle waves of different periods. */
		range_m = 5000U + ((triangle((uint32_t)azimuth * 3U) * 2000U) / 1024U) +
			  ((triangle((uint32_t)azimuth * 11U) * 600U) / 1024U);

		/* The coast recedes east and west of the bay own ship lies in. */
		if (from_north > AZ_DEG(45)) {
			range_m += ((from_north - AZ_DEG(45)) * 400U) / AZ_DEG(1);
		}
	}

	return range_m;
}

static uint32_t target_amplitude(const struct target *t, uint32_t revolution, uint16_t azimuth,
				 uint32_t range_m)
{
	uint32_t amplitude = 0U;
	uint32_t centre_m = t->range_m;
	uint32_t half_depth = t->depth_m / 2U;

	if (t->closing_m != 0U) {
		uint32_t travel_m =
			(revolution * t->closing_m) % (2U * (t->range_m - t->closest_m));

		/* Approach, then open out again, so the target never leaves the picture. */
		if (travel_m > (t->range_m - t->closest_m)) {
			travel_m = (2U * (t->range_m - t->closest_m)) - travel_m;
		}
		centre_m -= travel_m;
	}

	if (((range_m + half_depth) >= centre_m) && (range_m <= (centre_m + half_depth))) {
		/* Half the angle the target subtends, at least the 2 degree beam width. */
		uint32_t half_width =
			max_u32((t->length_m * AZ_PER_RAD) / (2U * centre_m), AZ_DEG(1));
		int32_t daz = azimuth_diff(azimuth, t->bearing);

		if (daz < 0) {
			daz = -daz;
		}

		if ((uint32_t)daz <= half_width) {
			amplitude = TARGET_PEAK -
				    (((uint32_t)daz * (TARGET_PEAK - TARGET_EDGE)) / half_width);
		}
	}

	return amplitude;
}

void scene_radial(const struct scene *scene, uint16_t azimuth, uint8_t *cells, size_t n)
{
	static const struct target targets[] = {
		/* A tanker at anchor off the coast. */
		{.bearing = AZ_DEG(118), .range_m = 4200U, .length_m = 90U, .depth_m = 60U},
		/* A fishing boat. */
		{.bearing = AZ_DEG(212), .range_m = 1800U, .length_m = 18U, .depth_m = 45U},
		/* A ferry closing in at 20 knots, 25 m per revolution at 24 rpm. */
		{.bearing = AZ_DEG(76),
		 .range_m = 6800U,
		 .length_m = 60U,
		 .depth_m = 60U,
		 .closing_m = 25U,
		 .closest_m = 1200U},
		/* A channel buoy. */
		{.bearing = AZ_DEG(22), .range_m = 900U, .length_m = 6U, .depth_m = 45U},
	};
	uint32_t coast_m = coast_range_m(azimuth);
	uint32_t spoke_key = (uint32_t)azimuth << 16;
	uint32_t sea_key = spoke_key ^ (scene->revolution * 0x9E3779B9U);

	for (size_t r = 0U; r < n; r++) {
		uint32_t range_m = (uint32_t)r * scene->cell_m;
		uint32_t sea = hash(sea_key ^ (uint32_t)r);
		uint32_t amplitude = sea & 0x0FU; /* receiver noise */

		if (range_m < CLUTTER_M) {
			/* Sea clutter: strong close in, speckled, new every revolution. */
			uint32_t level = ((CLUTTER_M - range_m) * CLUTTER_PEAK) / CLUTTER_M;

			amplitude = max_u32(amplitude, (level * ((sea >> 8) & 0xFFU)) / 255U);
		}

		if (range_m >= coast_m) {
			/* Land: a bright edge fading inland, with a texture that stays put. */
			uint32_t fade = (range_m - coast_m) / LAND_FADE_M;
			uint32_t level = (fade < (LAND_PEAK - LAND_FADE_MIN)) ? (LAND_PEAK - fade)
									      : LAND_FADE_MIN;
			uint32_t texture = 160U + (hash(spoke_key ^ (uint32_t)r) & 0x5FU);

			amplitude = max_u32(amplitude, (level * texture) / 255U);
		} else {
			for (size_t t = 0U; t < (sizeof(targets) / sizeof(targets[0])); t++) {
				amplitude = max_u32(amplitude,
						    target_amplitude(&targets[t], scene->revolution,
								     azimuth, range_m));
			}
		}

		cells[r] = (uint8_t)((amplitude > 255U) ? 255U : amplitude);
	}
}
