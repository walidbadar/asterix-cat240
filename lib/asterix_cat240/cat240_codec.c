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
/* FRN 1 of an FSPEC octet is its first bit. */
#define FSPEC_MSB           CAT240_BIT(7)

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

/* Read position in a buffer. */
struct cursor {
	const uint8_t *buf;
	size_t pos;
	size_t len;
};

/* Returns the next n octets and moves past them, or NULL when fewer are left. */
static const uint8_t *cursor_take(struct cursor *c, size_t n)
{
	const uint8_t *p = NULL;

	if ((c->len - c->pos) >= n) {
		p = &c->buf[c->pos];
		c->pos += n;
	}

	return p;
}

/* Takes the n octets of an item into *p, or fails with -EMSGSIZE. */
static int take_item(struct cursor *c, size_t n, const uint8_t **p)
{
	*p = cursor_take(c, n);

	return (*p == NULL) ? -EMSGSIZE : 0;
}

static bool exactly_one(uint16_t items, uint16_t mask)
{
	uint16_t present = items & mask;

	return (present != 0U) && ((present & (present - 1U)) == 0U);
}

static bool items_valid(uint8_t msg_type, uint16_t items)
{
	bool one_hdr;
	bool one_block;
	bool valid;

	switch (msg_type) {
	case ASTERIX_CAT240_MSG_VIDEO_SUMMARY:
		valid = ((items & ITEMS_SUMMARY_REQUIRED) == ITEMS_SUMMARY_REQUIRED) &&
			((items & ITEMS_SUMMARY_FORBIDDEN) == 0U);
		break;
	case ASTERIX_CAT240_MSG_VIDEO:
		one_hdr = exactly_one(items, ITEMS_VIDEO_HDR);
		one_block = exactly_one(items, ITEMS_VIDEO_BLOCK);
		valid = one_hdr && one_block &&
			((items & ITEMS_VIDEO_REQUIRED) == ITEMS_VIDEO_REQUIRED) &&
			((items & ITEMS_VIDEO_FORBIDDEN) == 0U);
		break;
	default:
		valid = false;
		break;
	}

	return valid;
}

static size_t fspec_len(uint16_t items)
{
	return ((items >> FSPEC_FRN_PER_OCTET) != 0U) ? 2U : 1U;
}

static void fspec_write(uint8_t *buf, uint16_t items)
{
	size_t n = fspec_len(items);

	for (size_t i = 0U; i < n; i++) {
		uint8_t octet = 0U;

		for (uint32_t bit = 0U; bit < FSPEC_FRN_PER_OCTET; bit++) {
			if ((items & CAT240_BIT16((i * FSPEC_FRN_PER_OCTET) + bit)) != 0U) {
				octet |= (uint8_t)(FSPEC_MSB >> bit);
			}
		}

		if ((i + 1U) < n) {
			octet |= FSPEC_FX;
		}

		buf[i] = octet;
	}
}

static int fspec_read(struct cursor *c, uint16_t *items)
{
	uint16_t found = 0U;
	size_t i = 0U;
	bool more = true;
	int ret = 0;

	while (more && (ret == 0)) {
		const uint8_t *octet = cursor_take(c, 1U);
		if (octet == NULL) {
			ret = -EMSGSIZE;
		} else {
			if (i >= FSPEC_UAP_LEN) {
				/* No FRN is defined past the second octet, so nothing can follow
				 * one. */
				if ((*octet & (uint8_t)~FSPEC_FX) != 0U) {
					ret = -EBADMSG;
				}
			} else {
				for (uint32_t bit = 0U; bit < FSPEC_FRN_PER_OCTET; bit++) {
					if ((*octet & (uint8_t)(FSPEC_MSB >> bit)) != 0U) {
						found |= CAT240_BIT16((i * FSPEC_FRN_PER_OCTET) +
								      bit);
					}
				}
			}

			more = (*octet & FSPEC_FX) != 0U;
			i++;
		}
	}

	if (ret == 0) {
		*items = found;
	}

	return ret;
}

size_t asterix_cat240_video_block_size(uint16_t items)
{
	size_t size;

	if ((items & ASTERIX_CAT240_ITEM_050) != 0U) {
		size = ASTERIX_CAT240_VIDEO_BLOCK_LOW_SIZE;
	} else if ((items & ASTERIX_CAT240_ITEM_051) != 0U) {
		size = ASTERIX_CAT240_VIDEO_BLOCK_MEDIUM_SIZE;
	} else if ((items & ASTERIX_CAT240_ITEM_052) != 0U) {
		size = ASTERIX_CAT240_VIDEO_BLOCK_HIGH_SIZE;
	} else {
		size = 0U;
	}

	return size;
}

size_t asterix_cat240_video_len(const struct asterix_cat240_record *rec)
{
	size_t len = 0U;

	if (rec != NULL) {
		len = (size_t)rec->video_rep * asterix_cat240_video_block_size(rec->items);
	}

	return len;
}

static int record_check(const struct asterix_cat240_record *rec)
{
	uint16_t items = rec->items | ITEMS_MANDATORY;
	size_t video_len = asterix_cat240_video_len(rec);
	bool valid = items_valid(rec->msg_type, items);

	if (valid && ((items & ASTERIX_CAT240_ITEM_030) != 0U)) {
		valid = (rec->summary != NULL) && (rec->summary_len != 0U);
	}

	if (valid && ((items & ASTERIX_CAT240_ITEM_048) != 0U)) {
		valid = asterix_cat240_bits_per_cell(rec->resolution) != 0U;
	}

	if (valid && ((items & ITEMS_VIDEO_BLOCK) != 0U)) {
		valid = (rec->video != NULL) && (rec->video_rep != 0U);

		if (valid && ((items & ASTERIX_CAT240_ITEM_052) != 0U)) {
			valid = rec->video_rep <= ASTERIX_CAT240_VIDEO_BLOCK_HIGH_REP_MAX;
		}
	}

	if (valid && ((items & ASTERIX_CAT240_ITEM_049) != 0U)) {
		valid = (rec->nb_cells <= ASTERIX_CAT240_NB_CELLS_MAX) && (rec->nb_vb <= video_len);
	}

	if (valid && ((items & ASTERIX_CAT240_ITEM_140) != 0U)) {
		valid = rec->time_of_day <= ASTERIX_CAT240_TIME_OF_DAY_MAX;
	}

	if (valid && ((items & ASTERIX_CAT240_ITEM_RE) != 0U)) {
		valid = (rec->re_len <= ASTERIX_CAT240_RE_SP_LEN_MAX) &&
			((rec->re_len == 0U) || (rec->re != NULL));
	}

	if (valid && ((items & ASTERIX_CAT240_ITEM_SP) != 0U)) {
		valid = (rec->sp_len <= ASTERIX_CAT240_RE_SP_LEN_MAX) &&
			((rec->sp_len == 0U) || (rec->sp != NULL));
	}

	return valid ? 0 : -EINVAL;
}

static size_t record_len(const struct asterix_cat240_record *rec, uint16_t items)
{
	size_t len = fspec_len(items) + ITEM_010_LEN + ITEM_000_LEN;

	if ((items & ASTERIX_CAT240_ITEM_020) != 0U) {
		len += ITEM_020_LEN;
	}

	if ((items & ASTERIX_CAT240_ITEM_030) != 0U) {
		len += ITEM_REP_LEN + (size_t)rec->summary_len;
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
		len += ITEM_REP_LEN + (size_t)rec->re_len;
	}

	if ((items & ASTERIX_CAT240_ITEM_SP) != 0U) {
		len += ITEM_REP_LEN + (size_t)rec->sp_len;
	}

	return len;
}

int asterix_cat240_encode_record(const struct asterix_cat240_record *rec, uint8_t *buf, size_t len)
{
	uint16_t items = 0U;
	int ret;

	if ((rec == NULL) || (buf == NULL)) {
		ret = -EINVAL;
	} else {
		ret = record_check(rec);
	}

	if (ret == 0) {
		items = rec->items | ITEMS_MANDATORY;
		if (record_len(rec, items) > len) {
			ret = -ENOBUFS;
		}
	}

	if (ret == 0) {
		size_t pos = fspec_len(items);

		fspec_write(buf, items);

		buf[pos] = rec->sac;
		buf[pos + 1U] = rec->sic;
		pos += ITEM_010_LEN;

		buf[pos] = rec->msg_type;
		pos += ITEM_000_LEN;

		if ((items & ASTERIX_CAT240_ITEM_020) != 0U) {
			cat240_put_be32(rec->msg_index, &buf[pos]);
			pos += ITEM_020_LEN;
		}

		if ((items & ASTERIX_CAT240_ITEM_030) != 0U) {
			buf[pos] = rec->summary_len;
			pos += ITEM_REP_LEN;
			(void)memcpy(&buf[pos], rec->summary, rec->summary_len);
			pos += rec->summary_len;
		}

		if ((items & ITEMS_VIDEO_HDR) != 0U) {
			cat240_put_be16(rec->video_hdr.start_az, &buf[pos]);
			cat240_put_be16(rec->video_hdr.end_az, &buf[pos + 2U]);
			cat240_put_be32(rec->video_hdr.start_rg, &buf[pos + 4U]);
			cat240_put_be32(rec->video_hdr.cell_dur, &buf[pos + 8U]);
			pos += ITEM_04X_LEN;
		}

		if ((items & ASTERIX_CAT240_ITEM_048) != 0U) {
			buf[pos] = rec->compressed ? ITEM_048_COMPRESSED : 0U;
			buf[pos + 1U] = rec->resolution;
			pos += ITEM_048_LEN;
		}

		if ((items & ASTERIX_CAT240_ITEM_049) != 0U) {
			cat240_put_be16(rec->nb_vb, &buf[pos]);
			cat240_put_be24(rec->nb_cells, &buf[pos + 2U]);
			pos += ITEM_049_LEN;
		}

		if ((items & ITEMS_VIDEO_BLOCK) != 0U) {
			size_t video_len = asterix_cat240_video_len(rec);

			buf[pos] = rec->video_rep;
			pos += ITEM_REP_LEN;
			(void)memcpy(&buf[pos], rec->video, rec->nb_vb);
			(void)memset(&buf[pos + rec->nb_vb], 0, video_len - rec->nb_vb);
			pos += video_len;
		}

		if ((items & ASTERIX_CAT240_ITEM_140) != 0U) {
			cat240_put_be24(rec->time_of_day, &buf[pos]);
			pos += ITEM_140_LEN;
		}

		if ((items & ASTERIX_CAT240_ITEM_RE) != 0U) {
			buf[pos] = rec->re_len + 1U;
			pos += ITEM_REP_LEN;
			if (rec->re_len > 0U) {
				(void)memcpy(&buf[pos], rec->re, rec->re_len);
				pos += rec->re_len;
			}
		}

		if ((items & ASTERIX_CAT240_ITEM_SP) != 0U) {
			buf[pos] = rec->sp_len + 1U;
			pos += ITEM_REP_LEN;
			if (rec->sp_len > 0U) {
				(void)memcpy(&buf[pos], rec->sp, rec->sp_len);
				pos += rec->sp_len;
			}
		}

		ret = (int)pos;
	}

	return ret;
}

int asterix_cat240_encode(const struct asterix_cat240_record *rec, uint8_t *buf, size_t len)
{
	int ret;

	if ((rec == NULL) || (buf == NULL)) {
		ret = -EINVAL;
	} else {
		ret = record_check(rec);
	}

	if (ret == 0) {
		size_t total = ASTERIX_CAT240_BLOCK_HDR_LEN +
			       record_len(rec, rec->items | ITEMS_MANDATORY);

		if (total > (size_t)UINT16_MAX) {
			ret = -EMSGSIZE;
		} else if (total > len) {
			ret = -ENOBUFS;
		} else {
			buf[0] = ASTERIX_CAT240_CAT;
			cat240_put_be16((uint16_t)total, &buf[1]);

			ret = asterix_cat240_encode_record(rec, &buf[ASTERIX_CAT240_BLOCK_HDR_LEN],
							   len - ASTERIX_CAT240_BLOCK_HDR_LEN);
			if (ret >= 0) {
				ret = (int)ASTERIX_CAT240_BLOCK_HDR_LEN + ret;
			}
		}
	}

	return ret;
}

int asterix_cat240_block_len(const uint8_t *buf, size_t len)
{
	int ret;

	if (buf == NULL) {
		ret = -EINVAL;
	} else if (len < ASTERIX_CAT240_BLOCK_HDR_LEN) {
		ret = -EMSGSIZE;
	} else if (buf[0] != ASTERIX_CAT240_CAT) {
		ret = -EBADMSG;
	} else {
		uint16_t block_len = cat240_get_be16(&buf[1]);

		if (block_len <= ASTERIX_CAT240_BLOCK_HDR_LEN) {
			ret = -EBADMSG;
		} else if (block_len > len) {
			ret = -EMSGSIZE;
		} else {
			ret = (int)block_len;
		}
	}

	return ret;
}

int asterix_cat240_decode_record(const uint8_t *buf, size_t len, struct asterix_cat240_record *rec)
{
	struct cursor c = {.buf = buf, .pos = 0U, .len = len};
	const uint8_t *p = NULL;
	int ret;

	if ((buf == NULL) || (rec == NULL)) {
		ret = -EINVAL;
	} else {
		(void)memset(rec, 0, sizeof(*rec));
		ret = fspec_read(&c, &rec->items);
	}

	if ((ret == 0) && ((rec->items & ITEMS_MANDATORY) != ITEMS_MANDATORY)) {
		ret = -EBADMSG;
	}

	if (ret == 0) {
		ret = take_item(&c, ITEM_010_LEN, &p);
	}

	if (ret == 0) {
		rec->sac = p[0];
		rec->sic = p[1];
		ret = take_item(&c, ITEM_000_LEN, &p);
	}

	if (ret == 0) {
		rec->msg_type = p[0];
		if (!items_valid(rec->msg_type, rec->items)) {
			ret = -EBADMSG;
		}
	}

	if ((ret == 0) && ((rec->items & ASTERIX_CAT240_ITEM_020) != 0U)) {
		ret = take_item(&c, ITEM_020_LEN, &p);
		if (ret == 0) {
			rec->msg_index = cat240_get_be32(p);
		}
	}

	if ((ret == 0) && ((rec->items & ASTERIX_CAT240_ITEM_030) != 0U)) {
		ret = take_item(&c, ITEM_REP_LEN, &p);
		if (ret == 0) {
			rec->summary_len = p[0];
			if (rec->summary_len == 0U) {
				ret = -EBADMSG;
			} else {
				ret = take_item(&c, rec->summary_len, &p);
			}
		}

		if (ret == 0) {
			rec->summary = (const char *)p;
		}
	}

	if ((ret == 0) && ((rec->items & ITEMS_VIDEO_HDR) != 0U)) {
		ret = take_item(&c, ITEM_04X_LEN, &p);
		if (ret == 0) {
			rec->video_hdr.start_az = cat240_get_be16(&p[0]);
			rec->video_hdr.end_az = cat240_get_be16(&p[2]);
			rec->video_hdr.start_rg = cat240_get_be32(&p[4]);
			rec->video_hdr.cell_dur = cat240_get_be32(&p[8]);
		}
	}

	if ((ret == 0) && ((rec->items & ASTERIX_CAT240_ITEM_048) != 0U)) {
		ret = take_item(&c, ITEM_048_LEN, &p);
		if (ret == 0) {
			rec->compressed = (p[0] & ITEM_048_COMPRESSED) != 0U;
			rec->resolution = p[1];
			if (asterix_cat240_bits_per_cell(rec->resolution) == 0U) {
				ret = -EBADMSG;
			}
		}
	}

	if ((ret == 0) && ((rec->items & ASTERIX_CAT240_ITEM_049) != 0U)) {
		ret = take_item(&c, ITEM_049_LEN, &p);
		if (ret == 0) {
			rec->nb_vb = cat240_get_be16(&p[0]);
			rec->nb_cells = cat240_get_be24(&p[2]);
		}
	}

	if ((ret == 0) && ((rec->items & ITEMS_VIDEO_BLOCK) != 0U)) {
		ret = take_item(&c, ITEM_REP_LEN, &p);
		if (ret == 0) {
			rec->video_rep = p[0];
			if (rec->video_rep == 0U) {
				ret = -EBADMSG;
			}
		}

		if (ret == 0) {
			size_t video_len = asterix_cat240_video_len(rec);

			if (rec->nb_vb > video_len) {
				ret = -EBADMSG;
			} else {
				ret = take_item(&c, video_len, &p);
			}
		}

		if (ret == 0) {
			rec->video = p;
		}
	}

	if ((ret == 0) && ((rec->items & ASTERIX_CAT240_ITEM_140) != 0U)) {
		ret = take_item(&c, ITEM_140_LEN, &p);
		if (ret == 0) {
			rec->time_of_day = cat240_get_be24(p);
		}
	}

	if ((ret == 0) && ((rec->items & ASTERIX_CAT240_ITEM_RE) != 0U)) {
		ret = take_item(&c, ITEM_REP_LEN, &p);
		if (ret == 0) {
			if (p[0] == 0U) {
				ret = -EBADMSG;
			} else {
				rec->re_len = p[0] - 1U;
				ret = take_item(&c, rec->re_len, &p);
			}
		}

		if (ret == 0) {
			rec->re = p;
		}
	}

	if ((ret == 0) && ((rec->items & ASTERIX_CAT240_ITEM_SP) != 0U)) {
		ret = take_item(&c, ITEM_REP_LEN, &p);
		if (ret == 0) {
			if (p[0] == 0U) {
				ret = -EBADMSG;
			} else {
				rec->sp_len = p[0] - 1U;
				ret = take_item(&c, rec->sp_len, &p);
			}
		}

		if (ret == 0) {
			rec->sp = p;
		}
	}

	if (ret == 0) {
		ret = (int)c.pos;
	}

	return ret;
}

int asterix_cat240_decode(const uint8_t *buf, size_t len, struct asterix_cat240_record *rec)
{
	int ret;

	if (rec == NULL) {
		ret = -EINVAL;
	} else {
		int block_len = asterix_cat240_block_len(buf, len);

		if (block_len < 0) {
			ret = block_len;
		} else {
			ret = asterix_cat240_decode_record(
				&buf[ASTERIX_CAT240_BLOCK_HDR_LEN],
				(size_t)block_len - ASTERIX_CAT240_BLOCK_HDR_LEN, rec);
			if (ret == -EMSGSIZE) {
				/* The whole block is there, so a record past LEN is malformed. */
				ret = -EBADMSG;
			} else if (ret >= 0) {
				ret = block_len;
			} else {
				/* Any other decoding error stands. */
			}
		}
	}

	return ret;
}
