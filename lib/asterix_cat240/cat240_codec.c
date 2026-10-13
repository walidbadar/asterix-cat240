/*
 * SPDX-FileCopyrightText: 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <errno.h>
#include <string.h>

#include "cat240_codec.h"

/* Each FSPEC octet names seven FRNs; its last bit is the FX extension. */
#define FSPEC_FRN_PER_OCTET 7U
/* The standard UAP defines 14 FRNs, so an FSPEC carries at most two octets of them. */
#define FSPEC_UAP_LEN       2U
#define FSPEC_FX            CAT240_BIT(0)

#define ITEM_010_LEN 2U
#define ITEM_000_LEN 1U
#define ITEM_020_LEN 4U
#define ITEM_04X_LEN 12U
#define ITEM_048_LEN 2U
#define ITEM_049_LEN 5U
#define ITEM_140_LEN 3U
/* Repetitive and variable length items start with a REP or length octet. */
#define ITEM_REP_LEN 1U

/* I240/048 bit 16, the first bit of the first octet. */
#define ITEM_048_COMPRESSED CAT240_BIT(7)

#define ITEMS_MANDATORY   (ASTERIX_CAT240_ITEM_010 | ASTERIX_CAT240_ITEM_000)
#define ITEMS_VIDEO_HDR   (ASTERIX_CAT240_ITEM_040 | ASTERIX_CAT240_ITEM_041)
#define ITEMS_VIDEO_BLOCK ASTERIX_CAT240_ITEM_VIDEO_BLOCK_MASK

/* Table 2 of the specification: items per message type. */
#define ITEMS_SUMMARY_REQUIRED ASTERIX_CAT240_ITEM_030
#define ITEMS_SUMMARY_FORBIDDEN                                                                    \
	(ASTERIX_CAT240_ITEM_020 | ITEMS_VIDEO_HDR | ASTERIX_CAT240_ITEM_048 |                     \
	 ASTERIX_CAT240_ITEM_049 | ITEMS_VIDEO_BLOCK)
#define ITEMS_VIDEO_REQUIRED                                                                       \
	(ASTERIX_CAT240_ITEM_020 | ASTERIX_CAT240_ITEM_048 | ASTERIX_CAT240_ITEM_049)
#define ITEMS_VIDEO_FORBIDDEN ASTERIX_CAT240_ITEM_030

struct cursor {
	const uint8_t *p;
	size_t left;
};

static const uint8_t *cursor_take(struct cursor *c, size_t n)
{
	const uint8_t *p = c->p;

	if (c->left < n) {
		return NULL;
	}

	c->p += n;
	c->left -= n;

	return p;
}

static bool exactly_one(uint16_t items, uint16_t mask)
{
	uint16_t present = items & mask;

	return (present != 0U) && ((present & (present - 1U)) == 0U);
}

static bool items_valid(uint8_t msg_type, uint16_t items)
{
	uint16_t required;
	uint16_t forbidden;

	switch (msg_type) {
	case ASTERIX_CAT240_MSG_VIDEO_SUMMARY:
		required = ITEMS_SUMMARY_REQUIRED;
		forbidden = ITEMS_SUMMARY_FORBIDDEN;
		break;
	case ASTERIX_CAT240_MSG_VIDEO:
		if (!exactly_one(items, ITEMS_VIDEO_HDR) ||
		    !exactly_one(items, ITEMS_VIDEO_BLOCK)) {
			return false;
		}
		required = ITEMS_VIDEO_REQUIRED;
		forbidden = ITEMS_VIDEO_FORBIDDEN;
		break;
	default:
		return false;
	}

	return ((items & required) == required) && ((items & forbidden) == 0U);
}

static size_t fspec_len(uint16_t items)
{
	return ((items >> FSPEC_FRN_PER_OCTET) != 0U) ? 2U : 1U;
}

static void fspec_write(uint8_t *buf, uint16_t items)
{
	size_t n = fspec_len(items);

	for (size_t i = 0; i < n; i++) {
		uint8_t octet = 0U;

		for (unsigned int bit = 0; bit < FSPEC_FRN_PER_OCTET; bit++) {
			if ((items & CAT240_BIT(i * FSPEC_FRN_PER_OCTET + bit)) != 0U) {
				octet |= CAT240_BIT(7) >> bit;
			}
		}

		if (i + 1U < n) {
			octet |= FSPEC_FX;
		}

		buf[i] = octet;
	}
}

static int fspec_read(struct cursor *c, uint16_t *items)
{
	uint16_t found = 0U;
	const uint8_t *octet;
	size_t i = 0;

	do {
		octet = cursor_take(c, 1U);
		if (octet == NULL) {
			return -EMSGSIZE;
		}

		if (i >= FSPEC_UAP_LEN) {
			/* No FRN is defined past the second octet, so nothing can follow one. */
			if ((*octet & (uint8_t)~FSPEC_FX) != 0U) {
				return -EBADMSG;
			}
		} else {
			for (unsigned int bit = 0; bit < FSPEC_FRN_PER_OCTET; bit++) {
				if ((*octet & (CAT240_BIT(7) >> bit)) != 0U) {
					found |= CAT240_BIT(i * FSPEC_FRN_PER_OCTET + bit);
				}
			}
		}

		i++;
	} while ((*octet & FSPEC_FX) != 0U);

	*items = found;

	return 0;
}

size_t asterix_cat240_video_block_size(uint16_t items)
{
	if ((items & ASTERIX_CAT240_ITEM_050) != 0U) {
		return ASTERIX_CAT240_VIDEO_BLOCK_LOW_SIZE;
	}

	if ((items & ASTERIX_CAT240_ITEM_051) != 0U) {
		return ASTERIX_CAT240_VIDEO_BLOCK_MEDIUM_SIZE;
	}

	if ((items & ASTERIX_CAT240_ITEM_052) != 0U) {
		return ASTERIX_CAT240_VIDEO_BLOCK_HIGH_SIZE;
	}

	return 0U;
}

size_t asterix_cat240_video_len(const struct asterix_cat240_record *rec)
{
	if (rec == NULL) {
		return 0U;
	}

	return (size_t)rec->video_rep * asterix_cat240_video_block_size(rec->items);
}

static int record_check(const struct asterix_cat240_record *rec)
{
	uint16_t items = rec->items | ITEMS_MANDATORY;

	if (!items_valid(rec->msg_type, items)) {
		return -EINVAL;
	}

	if ((items & ASTERIX_CAT240_ITEM_030) != 0U) {
		if ((rec->summary == NULL) || (rec->summary_len == 0U)) {
			return -EINVAL;
		}
	}

	if ((items & ASTERIX_CAT240_ITEM_048) != 0U) {
		if (asterix_cat240_bits_per_cell(rec->resolution) == 0U) {
			return -EINVAL;
		}
	}

	if ((items & ITEMS_VIDEO_BLOCK) != 0U) {
		if ((rec->video == NULL) || (rec->video_rep == 0U)) {
			return -EINVAL;
		}

		if (((items & ASTERIX_CAT240_ITEM_052) != 0U) &&
		    (rec->video_rep > ASTERIX_CAT240_VIDEO_BLOCK_HIGH_REP_MAX)) {
			return -EINVAL;
		}
	}

	if ((items & ASTERIX_CAT240_ITEM_049) != 0U) {
		if ((rec->nb_cells > ASTERIX_CAT240_NB_CELLS_MAX) ||
		    (rec->nb_vb > asterix_cat240_video_len(rec))) {
			return -EINVAL;
		}
	}

	if ((items & ASTERIX_CAT240_ITEM_140) != 0U) {
		if (rec->time_of_day > ASTERIX_CAT240_TIME_OF_DAY_MAX) {
			return -EINVAL;
		}
	}

	if ((items & ASTERIX_CAT240_ITEM_RE) != 0U) {
		if ((rec->re_len > ASTERIX_CAT240_RE_SP_LEN_MAX) ||
		    ((rec->re_len > 0U) && (rec->re == NULL))) {
			return -EINVAL;
		}
	}

	if ((items & ASTERIX_CAT240_ITEM_SP) != 0U) {
		if ((rec->sp_len > ASTERIX_CAT240_RE_SP_LEN_MAX) ||
		    ((rec->sp_len > 0U) && (rec->sp == NULL))) {
			return -EINVAL;
		}
	}

	return 0;
}

static size_t record_len(const struct asterix_cat240_record *rec, uint16_t items)
{
	size_t len = fspec_len(items) + ITEM_010_LEN + ITEM_000_LEN;

	if ((items & ASTERIX_CAT240_ITEM_020) != 0U) {
		len += ITEM_020_LEN;
	}

	if ((items & ASTERIX_CAT240_ITEM_030) != 0U) {
		len += ITEM_REP_LEN + rec->summary_len;
	}

	if ((items & ITEMS_VIDEO_HDR) != 0U) {
		len += ITEM_04X_LEN;
	}

	if ((items & ASTERIX_CAT240_ITEM_048) != 0U) {
		len += ITEM_048_LEN;
	}

	if ((items & ASTERIX_CAT240_ITEM_049) != 0U) {
		len += ITEM_049_LEN;
	}

	if ((items & ITEMS_VIDEO_BLOCK) != 0U) {
		len += ITEM_REP_LEN + asterix_cat240_video_len(rec);
	}

	if ((items & ASTERIX_CAT240_ITEM_140) != 0U) {
		len += ITEM_140_LEN;
	}

	if ((items & ASTERIX_CAT240_ITEM_RE) != 0U) {
		len += ITEM_REP_LEN + rec->re_len;
	}

	if ((items & ASTERIX_CAT240_ITEM_SP) != 0U) {
		len += ITEM_REP_LEN + rec->sp_len;
	}

	return len;
}

int asterix_cat240_encode_record(const struct asterix_cat240_record *rec, uint8_t *buf, size_t len)
{
	uint16_t items;
	uint8_t *p = buf;
	int ret;

	if ((rec == NULL) || (buf == NULL)) {
		return -EINVAL;
	}

	ret = record_check(rec);
	if (ret != 0) {
		return ret;
	}

	items = rec->items | ITEMS_MANDATORY;

	if (record_len(rec, items) > len) {
		return -ENOBUFS;
	}

	fspec_write(p, items);
	p += fspec_len(items);

	*p++ = rec->sac;
	*p++ = rec->sic;

	*p++ = rec->msg_type;

	if ((items & ASTERIX_CAT240_ITEM_020) != 0U) {
		cat240_put_be32(rec->msg_index, p);
		p += ITEM_020_LEN;
	}

	if ((items & ASTERIX_CAT240_ITEM_030) != 0U) {
		*p++ = rec->summary_len;
		memcpy(p, rec->summary, rec->summary_len);
		p += rec->summary_len;
	}

	if ((items & ITEMS_VIDEO_HDR) != 0U) {
		cat240_put_be16(rec->video_hdr.start_az, &p[0]);
		cat240_put_be16(rec->video_hdr.end_az, &p[2]);
		cat240_put_be32(rec->video_hdr.start_rg, &p[4]);
		cat240_put_be32(rec->video_hdr.cell_dur, &p[8]);
		p += ITEM_04X_LEN;
	}

	if ((items & ASTERIX_CAT240_ITEM_048) != 0U) {
		*p++ = rec->compressed ? ITEM_048_COMPRESSED : 0U;
		*p++ = rec->resolution;
	}

	if ((items & ASTERIX_CAT240_ITEM_049) != 0U) {
		cat240_put_be16(rec->nb_vb, &p[0]);
		cat240_put_be24(rec->nb_cells, &p[2]);
		p += ITEM_049_LEN;
	}

	if ((items & ITEMS_VIDEO_BLOCK) != 0U) {
		size_t video_len = asterix_cat240_video_len(rec);

		*p++ = rec->video_rep;
		memcpy(p, rec->video, rec->nb_vb);
		memset(&p[rec->nb_vb], 0, video_len - rec->nb_vb);
		p += video_len;
	}

	if ((items & ASTERIX_CAT240_ITEM_140) != 0U) {
		cat240_put_be24(rec->time_of_day, p);
		p += ITEM_140_LEN;
	}

	if ((items & ASTERIX_CAT240_ITEM_RE) != 0U) {
		*p++ = rec->re_len + 1U;
		if (rec->re_len > 0U) {
			memcpy(p, rec->re, rec->re_len);
			p += rec->re_len;
		}
	}

	if ((items & ASTERIX_CAT240_ITEM_SP) != 0U) {
		*p++ = rec->sp_len + 1U;
		if (rec->sp_len > 0U) {
			memcpy(p, rec->sp, rec->sp_len);
			p += rec->sp_len;
		}
	}

	return (int)(p - buf);
}

int asterix_cat240_encode(const struct asterix_cat240_record *rec, uint8_t *buf, size_t len)
{
	size_t total;
	int ret;

	if ((rec == NULL) || (buf == NULL)) {
		return -EINVAL;
	}

	ret = record_check(rec);
	if (ret != 0) {
		return ret;
	}

	total = ASTERIX_CAT240_BLOCK_HDR_LEN + record_len(rec, rec->items | ITEMS_MANDATORY);
	if (total > UINT16_MAX) {
		return -EMSGSIZE;
	}

	if (total > len) {
		return -ENOBUFS;
	}

	buf[0] = ASTERIX_CAT240_CAT;
	cat240_put_be16((uint16_t)total, &buf[1]);

	ret = asterix_cat240_encode_record(rec, &buf[ASTERIX_CAT240_BLOCK_HDR_LEN],
					   len - ASTERIX_CAT240_BLOCK_HDR_LEN);
	if (ret < 0) {
		return ret;
	}

	return (int)ASTERIX_CAT240_BLOCK_HDR_LEN + ret;
}

int asterix_cat240_block_len(const uint8_t *buf, size_t len)
{
	uint16_t block_len;

	if (buf == NULL) {
		return -EINVAL;
	}

	if (len < ASTERIX_CAT240_BLOCK_HDR_LEN) {
		return -EMSGSIZE;
	}

	if (buf[0] != ASTERIX_CAT240_CAT) {
		return -EBADMSG;
	}

	block_len = cat240_get_be16(&buf[1]);
	if (block_len <= ASTERIX_CAT240_BLOCK_HDR_LEN) {
		return -EBADMSG;
	}

	if (block_len > len) {
		return -EMSGSIZE;
	}

	return block_len;
}

int asterix_cat240_decode_record(const uint8_t *buf, size_t len, struct asterix_cat240_record *rec)
{
	struct cursor c = {.p = buf, .left = len};
	const uint8_t *p;
	int ret;

	if ((buf == NULL) || (rec == NULL)) {
		return -EINVAL;
	}

	memset(rec, 0, sizeof(*rec));

	ret = fspec_read(&c, &rec->items);
	if (ret != 0) {
		return ret;
	}

	if ((rec->items & ITEMS_MANDATORY) != ITEMS_MANDATORY) {
		return -EBADMSG;
	}

	p = cursor_take(&c, ITEM_010_LEN);
	if (p == NULL) {
		return -EMSGSIZE;
	}
	rec->sac = p[0];
	rec->sic = p[1];

	p = cursor_take(&c, ITEM_000_LEN);
	if (p == NULL) {
		return -EMSGSIZE;
	}
	rec->msg_type = p[0];

	if (!items_valid(rec->msg_type, rec->items)) {
		return -EBADMSG;
	}

	if ((rec->items & ASTERIX_CAT240_ITEM_020) != 0U) {
		p = cursor_take(&c, ITEM_020_LEN);
		if (p == NULL) {
			return -EMSGSIZE;
		}
		rec->msg_index = cat240_get_be32(p);
	}

	if ((rec->items & ASTERIX_CAT240_ITEM_030) != 0U) {
		p = cursor_take(&c, ITEM_REP_LEN);
		if (p == NULL) {
			return -EMSGSIZE;
		}
		rec->summary_len = p[0];
		if (rec->summary_len == 0U) {
			return -EBADMSG;
		}

		p = cursor_take(&c, rec->summary_len);
		if (p == NULL) {
			return -EMSGSIZE;
		}
		rec->summary = (const char *)p;
	}

	if ((rec->items & ITEMS_VIDEO_HDR) != 0U) {
		p = cursor_take(&c, ITEM_04X_LEN);
		if (p == NULL) {
			return -EMSGSIZE;
		}
		rec->video_hdr.start_az = cat240_get_be16(&p[0]);
		rec->video_hdr.end_az = cat240_get_be16(&p[2]);
		rec->video_hdr.start_rg = cat240_get_be32(&p[4]);
		rec->video_hdr.cell_dur = cat240_get_be32(&p[8]);
	}

	if ((rec->items & ASTERIX_CAT240_ITEM_048) != 0U) {
		p = cursor_take(&c, ITEM_048_LEN);
		if (p == NULL) {
			return -EMSGSIZE;
		}
		rec->compressed = (p[0] & ITEM_048_COMPRESSED) != 0U;
		rec->resolution = p[1];
		if (asterix_cat240_bits_per_cell(rec->resolution) == 0U) {
			return -EBADMSG;
		}
	}

	if ((rec->items & ASTERIX_CAT240_ITEM_049) != 0U) {
		p = cursor_take(&c, ITEM_049_LEN);
		if (p == NULL) {
			return -EMSGSIZE;
		}
		rec->nb_vb = cat240_get_be16(&p[0]);
		rec->nb_cells = cat240_get_be24(&p[2]);
	}

	if ((rec->items & ITEMS_VIDEO_BLOCK) != 0U) {
		size_t video_len;

		p = cursor_take(&c, ITEM_REP_LEN);
		if (p == NULL) {
			return -EMSGSIZE;
		}
		rec->video_rep = p[0];
		if (rec->video_rep == 0U) {
			return -EBADMSG;
		}

		video_len = asterix_cat240_video_len(rec);
		if (rec->nb_vb > video_len) {
			return -EBADMSG;
		}

		p = cursor_take(&c, video_len);
		if (p == NULL) {
			return -EMSGSIZE;
		}
		rec->video = p;
	}

	if ((rec->items & ASTERIX_CAT240_ITEM_140) != 0U) {
		p = cursor_take(&c, ITEM_140_LEN);
		if (p == NULL) {
			return -EMSGSIZE;
		}
		rec->time_of_day = cat240_get_be24(p);
	}

	if ((rec->items & ASTERIX_CAT240_ITEM_RE) != 0U) {
		p = cursor_take(&c, ITEM_REP_LEN);
		if (p == NULL) {
			return -EMSGSIZE;
		}
		if (p[0] == 0U) {
			return -EBADMSG;
		}
		rec->re_len = p[0] - 1U;

		p = cursor_take(&c, rec->re_len);
		if (p == NULL) {
			return -EMSGSIZE;
		}
		rec->re = p;
	}

	if ((rec->items & ASTERIX_CAT240_ITEM_SP) != 0U) {
		p = cursor_take(&c, ITEM_REP_LEN);
		if (p == NULL) {
			return -EMSGSIZE;
		}
		if (p[0] == 0U) {
			return -EBADMSG;
		}
		rec->sp_len = p[0] - 1U;

		p = cursor_take(&c, rec->sp_len);
		if (p == NULL) {
			return -EMSGSIZE;
		}
		rec->sp = p;
	}

	return (int)(len - c.left);
}

int asterix_cat240_decode(const uint8_t *buf, size_t len, struct asterix_cat240_record *rec)
{
	int block_len;
	int ret;

	if (rec == NULL) {
		return -EINVAL;
	}

	block_len = asterix_cat240_block_len(buf, len);
	if (block_len < 0) {
		return block_len;
	}

	ret = asterix_cat240_decode_record(&buf[ASTERIX_CAT240_BLOCK_HDR_LEN],
					   (size_t)block_len - ASTERIX_CAT240_BLOCK_HDR_LEN, rec);
	if (ret == -EMSGSIZE) {
		/* The whole block is there, so a record running past LEN is malformed. */
		return -EBADMSG;
	}

	if (ret < 0) {
		return ret;
	}

	return block_len;
}
