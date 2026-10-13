/*
 * SPDX-FileCopyrightText: 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <errno.h>
#include <stdbool.h>
#include <string.h>

#include "cat240_codec.h"
#include "cat240_port.h"

#if ASTERIX_CAT240_TIME_OF_DAY
/* The Time of Day item needs the real time clock: a deviation from Rule 21.10. */
#include <time.h>
#endif

#if defined(__ZEPHYR__)
LOG_MODULE_REGISTER(asterix_cat240, CONFIG_ASTERIX_CAT240_LOG_LEVEL);
#endif

/* Largest radial each video block item carries, from the specification. */
#define VIDEO_BLOCK_LOW_MAX_LEN    ((size_t)UINT8_MAX * ASTERIX_CAT240_VIDEO_BLOCK_LOW_SIZE)
#define VIDEO_BLOCK_MEDIUM_MAX_LEN ((size_t)UINT8_MAX * ASTERIX_CAT240_VIDEO_BLOCK_MEDIUM_SIZE)
#define VIDEO_BLOCK_HIGH_MAX_LEN                                                                   \
	(ASTERIX_CAT240_VIDEO_BLOCK_HIGH_REP_MAX * ASTERIX_CAT240_VIDEO_BLOCK_HIGH_SIZE)

/*
 * Socket address of either family; sockaddr_storage may be IPv4 sized on
 * Zephyr. The union lets the socket API see a struct sockaddr without a cast
 * between pointer types, at the cost of Rule 19.2.
 */
union cat240_sockaddr {
	struct sockaddr sa;
	struct sockaddr_in in;
	struct sockaddr_in6 in6;
};

#if ASTERIX_CAT240_TIME_OF_DAY
#define SECONDS_PER_DAY 86400
#define NSEC_PER_SECOND 1000000000ULL

static bool time_of_day(uint32_t *tod)
{
	struct timespec ts;
	bool valid = false;

	if (clock_gettime(CLOCK_REALTIME, &ts) == 0) {
		time_t secs = ts.tv_sec % (time_t)SECONDS_PER_DAY;
		uint64_t ticks =
			((uint64_t)secs * ASTERIX_CAT240_TIME_OF_DAY_HZ) +
			(((uint64_t)ts.tv_nsec * ASTERIX_CAT240_TIME_OF_DAY_HZ) / NSEC_PER_SECOND);

		*tod = (uint32_t)ticks;
		valid = true;
	}

	return valid;
}
#endif /* ASTERIX_CAT240_TIME_OF_DAY */

static int send_record(struct asterix_cat240 *ctx, struct asterix_cat240_record *rec)
{
	int len;
	ssize_t sent;
	int ret;

	rec->sac = ctx->sac;
	rec->sic = ctx->sic;

#if ASTERIX_CAT240_TIME_OF_DAY
	if (time_of_day(&rec->time_of_day)) {
		rec->items |= ASTERIX_CAT240_ITEM_140;
	}
#endif

	len = asterix_cat240_encode(rec, ctx->tx_buf, sizeof(ctx->tx_buf));
	if (len == -ENOBUFS) {
		ret = -EMSGSIZE;
	} else if (len < 0) {
		ret = len;
	} else {
		sent = send(ctx->sock, ctx->tx_buf, (size_t)len, 0);
		if (sent < 0) {
			ret = -errno;
			CAT240_LOG_DBG("send failed (%d)", ret);
		} else {
			ret = (int)sent;
		}
	}

	return ret;
}

/* Parses a numeric IPv4 or IPv6 address and returns its family, or 0. */
static int parse_addr(const char *text, uint16_t port, union cat240_sockaddr *addr,
		      socklen_t *addr_len, bool *mcast)
{
	struct sockaddr_in *in = &addr->in;
	struct sockaddr_in6 *in6 = &addr->in6;
	int family = 0;

	(void)memset(addr, 0, sizeof(*addr));

	if (inet_pton(AF_INET, text, &in->sin_addr) == 1) {
		in->sin_family = AF_INET;
		in->sin_port = htons(port);
		*addr_len = sizeof(*in);
		*mcast = (ntohl(in->sin_addr.s_addr) & 0xF0000000U) == 0xE0000000U;
		family = AF_INET;
	} else if (inet_pton(AF_INET6, text, &in6->sin6_addr) == 1) {
		in6->sin6_family = AF_INET6;
		in6->sin6_port = htons(port);
		*addr_len = sizeof(*in6);
		*mcast = in6->sin6_addr.s6_addr[0] == 0xFFU;
		family = AF_INET6;
	} else {
		/* Not a numeric address of either family. */
	}

	return family;
}

uint8_t asterix_cat240_bits_per_cell(enum asterix_cat240_resolution res)
{
	uint8_t bits = 0U;

	if ((res >= ASTERIX_CAT240_RES_MONOBIT) && (res <= ASTERIX_CAT240_RES_ULTRA_HIGH)) {
		/* Each resolution step doubles the coding length, from 1 bit to 32. */
		uint32_t step = (uint32_t)res - (uint32_t)ASTERIX_CAT240_RES_MONOBIT;

		bits = (uint8_t)CAT240_BIT(step);
	}

	return bits;
}

int asterix_cat240_init(struct asterix_cat240 *ctx, const struct asterix_cat240_config *cfg)
{
	union cat240_sockaddr addr;
	socklen_t addr_len = 0U;
	bool mcast = false;
	int family = 0;
	int sock = -1;
	int ret = 0;

	if ((ctx == NULL) || (cfg == NULL) || (cfg->addr == NULL) || (cfg->port == 0U)) {
		ret = -EINVAL;
	} else {
		family = parse_addr(cfg->addr, cfg->port, &addr, &addr_len, &mcast);
		if (family == 0) {
			ret = -EINVAL;
		}
	}

	if (ret == 0) {
		sock = socket(family, SOCK_DGRAM, IPPROTO_UDP);
		if (sock < 0) {
			ret = -errno;
			CAT240_LOG_ERR("socket failed (%d)", ret);
		}
	}

	if ((ret == 0) && (cfg->iface != NULL) && (cfg->iface[0] != '\0')) {
		ret = cat240_bind_to_device(sock, cfg->iface);
		if (ret < 0) {
			CAT240_LOG_ERR("bind to %s failed (%d)", cfg->iface, ret);
		}
	}

	if ((ret == 0) && mcast) {
		int ttl = ASTERIX_CAT240_MCAST_TTL;
		int level = (family == AF_INET) ? IPPROTO_IP : IPPROTO_IPV6;
		int optname = (family == AF_INET) ? IP_MULTICAST_TTL : IPV6_MULTICAST_HOPS;

		if (setsockopt(sock, level, optname, &ttl, sizeof(ttl)) < 0) {
			CAT240_LOG_WRN("multicast TTL not set (%d)", -errno);
		}
	}

	if (ret == 0) {
		if (connect(sock, &addr.sa, addr_len) < 0) {
			ret = -errno;
			CAT240_LOG_ERR("connect to %s:%u failed (%d)", cfg->addr, cfg->port, ret);
		}
	}

	if ((ret != 0) && (sock >= 0)) {
		(void)close(sock);
	}

	if (ret == 0) {
		(void)memset(ctx, 0, sizeof(*ctx));
		ctx->open = true;
		ctx->sock = sock;
		ctx->sac = cfg->sac;
		ctx->sic = cfg->sic;

		CAT240_LOG_INF(
			"stream %u/%u to %s:%u via %s", cfg->sac, cfg->sic, cfg->addr, cfg->port,
			((cfg->iface != NULL) && (cfg->iface[0] != '\0')) ? cfg->iface : "any");
	}

	return ret;
}

int asterix_cat240_send_summary(struct asterix_cat240 *ctx, const char *text)
{
	struct asterix_cat240_record rec = {
		.items = ASTERIX_CAT240_ITEM_030,
		.msg_type = ASTERIX_CAT240_MSG_VIDEO_SUMMARY,
		.summary = text,
	};
	int ret;

	if ((ctx == NULL) || !ctx->open || (text == NULL)) {
		ret = -EINVAL;
	} else {
		size_t len = strlen(text);

		if ((len == 0U) || (len > (size_t)UINT8_MAX)) {
			ret = -EINVAL;
		} else {
			rec.summary_len = (uint8_t)len;
			ret = send_record(ctx, &rec);
		}
	}

	return ret;
}

int asterix_cat240_send_video(struct asterix_cat240 *ctx, const struct asterix_cat240_video *video)
{
	struct asterix_cat240_record rec = {
		.items =
			ASTERIX_CAT240_ITEM_020 | ASTERIX_CAT240_ITEM_048 | ASTERIX_CAT240_ITEM_049,
		.msg_type = ASTERIX_CAT240_MSG_VIDEO,
	};
	uint64_t nb_cells = 0U;
	uint8_t bits = 0U;
	size_t block_size = 0U;
	int ret = 0;

	if ((ctx == NULL) || !ctx->open || (video == NULL) || (video->cells == NULL) ||
	    (video->len == 0U)) {
		ret = -EINVAL;
	} else {
		bits = asterix_cat240_bits_per_cell(video->resolution);
		if (bits == 0U) {
			ret = -EINVAL;
		}
	}

	if (ret == 0) {
		nb_cells = video->nb_cells;
		if (nb_cells == 0U) {
			nb_cells = ((uint64_t)video->len * 8U) / bits;
		}

		if ((nb_cells > ASTERIX_CAT240_NB_CELLS_MAX) ||
		    ((nb_cells * bits) > ((uint64_t)video->len * 8U))) {
			ret = -EINVAL;
		}
	}

	if (ret == 0) {
		if (video->len <= VIDEO_BLOCK_LOW_MAX_LEN) {
			rec.items |= ASTERIX_CAT240_ITEM_050;
			block_size = ASTERIX_CAT240_VIDEO_BLOCK_LOW_SIZE;
		} else if (video->len <= VIDEO_BLOCK_MEDIUM_MAX_LEN) {
			rec.items |= ASTERIX_CAT240_ITEM_051;
			block_size = ASTERIX_CAT240_VIDEO_BLOCK_MEDIUM_SIZE;
		} else if (video->len <= VIDEO_BLOCK_HIGH_MAX_LEN) {
			rec.items |= ASTERIX_CAT240_ITEM_052;
			block_size = ASTERIX_CAT240_VIDEO_BLOCK_HIGH_SIZE;
		} else {
			ret = -EMSGSIZE;
		}
	}

	if (ret == 0) {
		rec.items |= video->femto ? ASTERIX_CAT240_ITEM_041 : ASTERIX_CAT240_ITEM_040;
		rec.msg_index = ctx->msg_index;
		rec.video_hdr.start_az = video->start_az;
		rec.video_hdr.end_az = video->end_az;
		rec.video_hdr.start_rg = video->start_rg;
		rec.video_hdr.cell_dur = video->cell_dur;
		rec.compressed = video->compressed;
		rec.resolution = video->resolution;
		rec.nb_vb = (uint16_t)video->len;
		rec.nb_cells = (uint32_t)nb_cells;
		rec.video_rep = (uint8_t)CAT240_DIV_ROUND_UP(video->len, block_size);
		rec.video = video->cells;

		ret = send_record(ctx, &rec);
		if (ret >= 0) {
			ctx->msg_index++;
		}
	}

	return ret;
}

int asterix_cat240_close(struct asterix_cat240 *ctx)
{
	int ret = 0;

	if ((ctx == NULL) || !ctx->open) {
		ret = -EINVAL;
	} else {
		(void)close(ctx->sock);
		ctx->open = false;
		ctx->sock = -1;
	}

	return ret;
}
