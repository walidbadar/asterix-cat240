/*
 * SPDX-FileCopyrightText: 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

/**
 * @file
 * @brief ASTERIX Category 240 radar video over UDP
 */

#ifndef ASTERIX_CAT240_H_
#define ASTERIX_CAT240_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup asterix_cat240 ASTERIX Category 240 radar video
 * @brief Send ASTERIX Category 240 radar video messages over UDP
 *
 * Implements EUROCONTROL-SPEC-0149-240 edition 1.3. A stream is bound to one
 * destination, unicast or multicast, IPv4 or IPv6, and to one data source
 * identifier. asterix_cat240_send_video() turns one radial into a video
 * message and sends it as one UDP datagram; asterix_cat240_send_summary()
 * does the same for a video summary message. The library fills in the
 * message sequence numbers and, with @ref ASTERIX_CAT240_TIME_OF_DAY, the
 * time of day.
 *
 * The implementation uses POSIX sockets, clocks and sleep only, so it builds
 * for Zephyr and for Linux or any other POSIX system alike. A stream is used
 * from one thread at a time.
 * @{
 */

/** Number of azimuth steps in a full turn: the LSB of an azimuth is 360/65536 degrees. */
#define ASTERIX_CAT240_AZIMUTH_STEPS 65536U

/**
 * @name Build time configuration
 *
 * On Zephyr these follow the ``CONFIG_ASTERIX_CAT240_*`` Kconfig options. On
 * other systems they take the defaults below unless defined on the compiler
 * command line.
 * @{
 */
#if !defined(ASTERIX_CAT240_TX_BUF_SIZE)
#if defined(CONFIG_ASTERIX_CAT240_TX_BUF_SIZE)
#define ASTERIX_CAT240_TX_BUF_SIZE CONFIG_ASTERIX_CAT240_TX_BUF_SIZE
#else
/** Size in octets of the buffer each stream holds for one message. */
#define ASTERIX_CAT240_TX_BUF_SIZE 1472
#endif
#endif

#if !defined(ASTERIX_CAT240_MCAST_TTL)
#if defined(CONFIG_ASTERIX_CAT240_MCAST_TTL)
#define ASTERIX_CAT240_MCAST_TTL CONFIG_ASTERIX_CAT240_MCAST_TTL
#else
/** TTL or hop limit of the datagrams sent to a multicast destination. */
#define ASTERIX_CAT240_MCAST_TTL 1
#endif
#endif

#if !defined(ASTERIX_CAT240_TIME_OF_DAY)
#if defined(__ZEPHYR__) && !defined(CONFIG_ASTERIX_CAT240_TIME_OF_DAY)
#define ASTERIX_CAT240_TIME_OF_DAY 0
#else
/** Add the time of day, from the real time clock, to every message. */
#define ASTERIX_CAT240_TIME_OF_DAY 1
#endif
#endif
/** @} */

/** Bit resolution of the video cell amplitude. */
enum asterix_cat240_resolution {
	/** Monobit resolution, 1 bit per cell. */
	ASTERIX_CAT240_RES_MONOBIT = 1,
	/** Low resolution, 2 bits per cell. */
	ASTERIX_CAT240_RES_LOW = 2,
	/** Medium resolution, 4 bits per cell. */
	ASTERIX_CAT240_RES_MEDIUM = 3,
	/** High resolution, 8 bits per cell. */
	ASTERIX_CAT240_RES_HIGH = 4,
	/** Very high resolution, 16 bits per cell. */
	ASTERIX_CAT240_RES_VERY_HIGH = 5,
	/** Ultra high resolution, 32 bits per cell. */
	ASTERIX_CAT240_RES_ULTRA_HIGH = 6,
};

/** Destination and identity of a radar video stream. */
struct asterix_cat240_config {
	/** Destination IPv4 or IPv6 address, unicast or multicast, as text. */
	const char *addr;
	/** Destination UDP port. */
	uint16_t port;
	/**
	 * Name of the network interface to send from, such as "eth0", or NULL
	 * to let the system route the datagrams.
	 */
	const char *iface;
	/** System Area Code of the data source. */
	uint8_t sac;
	/** System Identification Code of the data source. */
	uint8_t sic;
};

/**
 * @brief One radial of video cells
 *
 * The cells are consecutive in range, the first one being the closest to the
 * radar. The range in meters of cell n, counted from 1, is
 * cell_dur * (start_rg + n - 1) * c / 2 with cell_dur in seconds and
 * c = 299792458 m/s.
 */
struct asterix_cat240_video {
	/** Start azimuth of the radial, LSB 360/65536 degrees. */
	uint16_t start_az;
	/** End azimuth of the radial, LSB 360/65536 degrees. */
	uint16_t end_az;
	/** Range of the first cell, in cells, 0 being the radar location. */
	uint32_t start_rg;
	/** Cell duration in nanoseconds, or in femtoseconds when @ref femto is set.
	 */
	uint32_t cell_dur;
	/** @ref cell_dur is in femtoseconds. */
	bool femto;
	/** Bits per cell of @ref cells. */
	enum asterix_cat240_resolution resolution;
	/** A data compression technique has been applied to @ref cells. */
	bool compressed;
	/** Cell amplitudes, packed at @ref resolution bits per cell. */
	const uint8_t *cells;
	/** Number of octets at @ref cells. */
	size_t len;
	/** Number of cells, or 0 to derive it from @ref len and @ref resolution. */
	uint32_t nb_cells;
};

/**
 * @brief An 8 bit Cartesian image to send as radar video
 *
 * The image is resampled into @ref radials radials of one cell per pixel,
 * each starting at the pixel of @ref center_x, @ref center_y and walking
 * outward until it leaves the image. Azimuth 0 points up the image, towards
 * decreasing rows, and increases clockwise. The radial of sector i starts at
 * azimuth i * 65536 / radials and is sampled along that azimuth.
 */
struct asterix_cat240_cartesian_video {
	/** Pixel amplitudes, row major, one octet each. */
	const uint8_t *pixels;
	/** Image width in pixels. */
	uint16_t width;
	/** Image height in pixels. */
	uint16_t height;
	/** Column of the radar position. */
	uint16_t center_x;
	/** Row of the radar position. */
	uint16_t center_y;
	/**
	 * Round trip time of light over one pixel, 2 * size / c, in nanoseconds
	 * or in femtoseconds when @ref femto is set. A 10 m pixel is 66713000 fs.
	 */
	uint32_t pixel_dur;
	/** @ref pixel_dur is in femtoseconds. */
	bool femto;
	/** Number of radials per turn, at least one. */
	uint16_t radials;
	/** Pause between two radials in microseconds, 0 to send back to back. */
	uint32_t radial_interval_us;
	/** Scratch buffer for one radial. */
	uint8_t *radial;
	/**
	 * Size of @ref radial in octets: at least the distance in pixels from the
	 * center to the farthest corner, plus one.
	 */
	size_t radial_size;
};

/** Radar video stream, set up by asterix_cat240_init(). */
struct asterix_cat240 {
	/** @cond INTERNAL_HIDDEN */
	bool open;
	int sock;
	uint8_t sac;
	uint8_t sic;
	uint32_t msg_index;
	uint8_t tx_buf[ASTERIX_CAT240_TX_BUF_SIZE];
	/** @endcond */
};

/**
 * @brief Get the number of bits coding one cell amplitude
 *
 * @param res Resolution.
 *
 * @return Bits per cell, or 0 when @p res is not a defined resolution.
 */
uint8_t asterix_cat240_bits_per_cell(enum asterix_cat240_resolution res);

/**
 * @brief Open a radar video stream
 *
 * Creates a UDP socket connected to the destination, bound to the interface
 * named in the configuration when one is given. A multicast destination gets
 * @ref ASTERIX_CAT240_MCAST_TTL as TTL or hop limit.
 *
 * @param ctx Stream to set up.
 * @param cfg Destination, interface and data source identifier.
 *
 * @retval 0 Success.
 * @retval -EINVAL @p ctx or @p cfg is NULL, the port is 0, the address is not
 *         an IP address, or the interface name is too long.
 * @retval -EAFNOSUPPORT The address family of the destination is not enabled.
 * @retval -ENODEV No interface of the given name exists.
 * @retval -errno The socket could not be created, bound or connected, with
 *         the errno value the socket API set.
 */
int asterix_cat240_init(struct asterix_cat240 *ctx, const struct asterix_cat240_config *cfg);

/**
 * @brief Send a video summary message
 *
 * @param ctx Open stream.
 * @param text Stream meta data as ASCII text, 1 to 255 characters.
 *
 * @retval >0 Number of octets sent.
 * @retval -EINVAL @p ctx is NULL or not open, or @p text is NULL, empty or
 *         too long.
 * @retval -errno The datagram could not be sent, with the errno value the
 *         socket API set.
 */
int asterix_cat240_send_summary(struct asterix_cat240 *ctx, const char *text);

/**
 * @brief Send one radial as a video message
 *
 * The video block item is chosen from the radial length: I240/050 up to 1020
 * octets, I240/051 up to 16320 octets and I240/052 up to 65024 octets. The
 * last block is zero padded and the counters report the valid octets and
 * cells. Each call consumes one message sequence number.
 *
 * @param ctx Open stream.
 * @param video Radial to send.
 *
 * @retval >0 Number of octets sent.
 * @retval -EINVAL @p ctx is NULL or not open, @p video or its cells are NULL,
 *         the length is 0, the resolution is undefined, or more cells are
 *         claimed than the octets hold.
 * @retval -EMSGSIZE The message does not fit the largest video block item or
 *         @ref ASTERIX_CAT240_TX_BUF_SIZE.
 * @retval -errno The datagram could not be sent, with the errno value the
 *         socket API set.
 */
int asterix_cat240_send_video(struct asterix_cat240 *ctx, const struct asterix_cat240_video *video);

/**
 * @brief Send a Cartesian image as one turn of radar video
 *
 * Resamples the image into radials and sends each one as
 * asterix_cat240_send_video() does, at high resolution (8 bits per cell).
 * On Zephyr it requires ``CONFIG_ASTERIX_CAT240_CARTESIAN``.
 *
 * @param ctx Open stream.
 * @param image Image to send.
 *
 * @retval >0 Number of radials sent.
 * @retval -EINVAL @p ctx is NULL or not open, @p image or its buffers are
 *         NULL, the image is empty, the center lies outside it, the pixel
 *         duration or the radial count is 0.
 * @retval -EMSGSIZE A radial does not fit the scratch buffer, the largest
 *         video block item or @ref ASTERIX_CAT240_TX_BUF_SIZE.
 * @retval -errno A datagram could not be sent, with the errno value the
 *         socket API set.
 */
int asterix_cat240_send_cartesian_video(struct asterix_cat240 *ctx,
					const struct asterix_cat240_cartesian_video *image);

/**
 * @brief Close a radar video stream
 *
 * @param ctx Open stream.
 *
 * @retval 0 Success.
 * @retval -EINVAL @p ctx is NULL or not open.
 */
int asterix_cat240_close(struct asterix_cat240 *ctx);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* ASTERIX_CAT240_H_ */
