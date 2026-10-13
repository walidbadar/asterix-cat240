/*
 * SPDX-FileCopyrightText: 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

/*
 * Platform layer: the library is written against POSIX sockets, clocks and
 * sleep. Zephyr provides them through its POSIX headers; any other POSIX
 * system through the standard ones. Logging exists on Zephyr only.
 */

#ifndef ASTERIX_CAT240_PORT_H_
#define ASTERIX_CAT240_PORT_H_

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#if defined(__ZEPHYR__)
#include <zephyr/logging/log.h>
#include <zephyr/posix/arpa/inet.h>
#include <zephyr/posix/netinet/in.h>
#include <zephyr/posix/sys/socket.h>
#include <zephyr/posix/net/if.h>
#include <zephyr/posix/unistd.h>
#include <time.h>

#define CAT240_LOG_ERR(...) LOG_ERR(__VA_ARGS__)
#define CAT240_LOG_WRN(...) LOG_WRN(__VA_ARGS__)
#define CAT240_LOG_INF(...) LOG_INF(__VA_ARGS__)
#define CAT240_LOG_DBG(...) LOG_DBG(__VA_ARGS__)
#else
#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define CAT240_LOG_ERR(...) ((void)0)
#define CAT240_LOG_WRN(...) ((void)0)
#define CAT240_LOG_INF(...) ((void)0)
#define CAT240_LOG_DBG(...) ((void)0)
#endif

/*
 * Binds the socket to the named interface. Zephyr takes an ifreq structure
 * and Linux the bare name. Returns 0 or a negative errno value.
 */
static inline int cat240_bind_to_device(int sock, const char *name)
{
#if defined(__ZEPHYR__)
	struct net_ifreq ifreq;

	if (strlen(name) >= sizeof(ifreq.ifr_name)) {
		return -EINVAL;
	}

	memset(&ifreq, 0, sizeof(ifreq));
	strcpy(ifreq.ifr_name, name);

	if (setsockopt(sock, SOL_SOCKET, SO_BINDTODEVICE, &ifreq, sizeof(ifreq)) < 0) {
		/* Zephyr reports an unknown interface as ENOENT, Linux as ENODEV. */
		return (errno == ENOENT) ? -ENODEV : -errno;
	}
#else
	size_t len = strlen(name);

	if (len >= IFNAMSIZ) {
		return -EINVAL;
	}

	if (setsockopt(sock, SOL_SOCKET, SO_BINDTODEVICE, name, (socklen_t)(len + 1U)) < 0) {
		return -errno;
	}
#endif

	return 0;
}

#define CAT240_BIT(n)             (1U << (n))
#define CAT240_DIV_ROUND_UP(n, d) (((n) + (d) - 1U) / (d))

static inline void cat240_put_be16(uint16_t val, uint8_t *dst)
{
	dst[0] = (uint8_t)(val >> 8);
	dst[1] = (uint8_t)val;
}

static inline void cat240_put_be24(uint32_t val, uint8_t *dst)
{
	dst[0] = (uint8_t)(val >> 16);
	dst[1] = (uint8_t)(val >> 8);
	dst[2] = (uint8_t)val;
}

static inline void cat240_put_be32(uint32_t val, uint8_t *dst)
{
	dst[0] = (uint8_t)(val >> 24);
	dst[1] = (uint8_t)(val >> 16);
	dst[2] = (uint8_t)(val >> 8);
	dst[3] = (uint8_t)val;
}

static inline uint16_t cat240_get_be16(const uint8_t *src)
{
	return (uint16_t)(((uint16_t)src[0] << 8) | src[1]);
}

static inline uint32_t cat240_get_be24(const uint8_t *src)
{
	return ((uint32_t)src[0] << 16) | ((uint32_t)src[1] << 8) | src[2];
}

static inline uint32_t cat240_get_be32(const uint8_t *src)
{
	return ((uint32_t)src[0] << 24) | ((uint32_t)src[1] << 16) | ((uint32_t)src[2] << 8) |
	       src[3];
}

#endif /* ASTERIX_CAT240_PORT_H_ */
