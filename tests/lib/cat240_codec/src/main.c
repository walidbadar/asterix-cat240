/*
 * SPDX-FileCopyrightText: 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <errno.h>
#include <string.h>

#include <zephyr/ztest.h>

#include "cat240_codec.h"

static const uint8_t video_cells[] = {0, 1, 2, 3, 4, 5, 6, 7};

/* Video message: nano video header, low data volume block, time of day. */
static const uint8_t video_block[] = {
	0xF0, 0x00, 0x2B,                                     /* CAT 240, LEN 43 */
	0xEB, 0xC8,                                           /* FSPEC: FRN 1 2 3 5 7, 8 9 12 */
	0x12, 0x34,                                           /* I240/010 SAC, SIC */
	0x02,                                                 /* I240/000 video message */
	0x01, 0x02, 0x03, 0x04,                               /* I240/020 MSG_INDEX */
	0x10, 0x00, 0x11, 0x00,                               /* I240/040 START_AZ, END_AZ */
	0x00, 0x00, 0x00, 0x05,                               /* START_RG */
	0x00, 0x00, 0x00, 0xC8,                               /* CELL_DUR 200 ns */
	0x00, 0x04,                                           /* I240/048 uncompressed, high */
	0x00, 0x08, 0x00, 0x00, 0x08,                         /* I240/049 NB_VB 8, NB_CELLS 8 */
	0x02, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, /* I240/050 REP 2 */
	0x12, 0x34, 0x56,                                     /* I240/140 */
};
#define VIDEO_BLOCK_VIDEO_OFFSET 32

static const struct asterix_cat240_record video_rec = {
	.items = ASTERIX_CAT240_ITEM_020 | ASTERIX_CAT240_ITEM_040 | ASTERIX_CAT240_ITEM_048 |
		 ASTERIX_CAT240_ITEM_049 | ASTERIX_CAT240_ITEM_050 | ASTERIX_CAT240_ITEM_140,
	.sac = 0x12,
	.sic = 0x34,
	.msg_type = ASTERIX_CAT240_MSG_VIDEO,
	.msg_index = 0x01020304,
	.video_hdr = {
		.start_az = 0x1000,
		.end_az = 0x1100,
		.start_rg = 5,
		.cell_dur = 200,
	},
	.compressed = false,
	.resolution = ASTERIX_CAT240_RES_HIGH,
	.nb_vb = 8,
	.nb_cells = 8,
	.video_rep = 2,
	.video = video_cells,
	.time_of_day = 0x123456,
};

/* Video summary message without optional items: a one octet FSPEC. */
static const uint8_t summary_block[] = {
	0xF0, 0x00, 0x0B,    /* CAT 240, LEN 11 */
	0xD0,                /* FSPEC: FRN 1 2 4 */
	0x01, 0x02,          /* I240/010 SAC, SIC */
	0x01,                /* I240/000 video summary message */
	0x03, 'P', 'S', 'R', /* I240/030 REP 3 */
};
#define SUMMARY_BLOCK_TEXT_OFFSET 8

static const struct asterix_cat240_record summary_rec = {
	.items = ASTERIX_CAT240_ITEM_030,
	.sac = 0x01,
	.sic = 0x02,
	.msg_type = ASTERIX_CAT240_MSG_VIDEO_SUMMARY,
	.summary = "PSR",
	.summary_len = 3,
};

/*
 * Two high data volume blocks: the records that claim more are rejected before
 * any cell is read.
 */
static uint8_t big_cells[2 * ASTERIX_CAT240_VIDEO_BLOCK_HIGH_SIZE];
static uint8_t buf[2 * ASTERIX_CAT240_VIDEO_BLOCK_HIGH_SIZE + 64];

static void check_video_rec(const struct asterix_cat240_record *rec, const uint8_t *video)
{
	zassert_equal(rec->items,
		      video_rec.items | ASTERIX_CAT240_ITEM_010 | ASTERIX_CAT240_ITEM_000);
	zassert_equal(rec->sac, 0x12);
	zassert_equal(rec->sic, 0x34);
	zassert_equal(rec->msg_type, ASTERIX_CAT240_MSG_VIDEO);
	zassert_equal(rec->msg_index, 0x01020304);
	zassert_equal(rec->video_hdr.start_az, 0x1000);
	zassert_equal(rec->video_hdr.end_az, 0x1100);
	zassert_equal(rec->video_hdr.start_rg, 5);
	zassert_equal(rec->video_hdr.cell_dur, 200);
	zassert_false(rec->compressed);
	zassert_equal(rec->resolution, ASTERIX_CAT240_RES_HIGH);
	zassert_equal(rec->nb_vb, 8);
	zassert_equal(rec->nb_cells, 8);
	zassert_equal(rec->video_rep, 2);
	zassert_equal_ptr(rec->video, video);
	zassert_equal(asterix_cat240_video_len(rec), sizeof(video_cells));
	zassert_mem_equal(rec->video, video_cells, sizeof(video_cells));
	zassert_equal(rec->time_of_day, 0x123456);
}

ZTEST(cat240_codec, test_encode_video)
{
	int ret;

	ret = asterix_cat240_encode(&video_rec, buf, sizeof(buf));
	zassert_equal(ret, sizeof(video_block), "encode returned %d", ret);
	zassert_mem_equal(buf, video_block, sizeof(video_block));
}

ZTEST(cat240_codec, test_encode_summary)
{
	int ret;

	ret = asterix_cat240_encode(&summary_rec, buf, sizeof(buf));
	zassert_equal(ret, sizeof(summary_block), "encode returned %d", ret);
	zassert_mem_equal(buf, summary_block, sizeof(summary_block));
}

ZTEST(cat240_codec, test_encode_record)
{
	size_t rec_len = sizeof(video_block) - ASTERIX_CAT240_BLOCK_HDR_LEN;
	int ret;

	ret = asterix_cat240_encode_record(&video_rec, buf, sizeof(buf));
	zassert_equal(ret, rec_len, "encode_record returned %d", ret);
	zassert_mem_equal(buf, &video_block[ASTERIX_CAT240_BLOCK_HDR_LEN], rec_len);

	/* The buffer must hold the whole record, and nothing less. */
	ret = asterix_cat240_encode_record(&video_rec, buf, rec_len);
	zassert_equal(ret, rec_len);
	ret = asterix_cat240_encode_record(&video_rec, buf, rec_len - 1);
	zassert_equal(ret, -ENOBUFS);
}

ZTEST(cat240_codec, test_encode_implicit_mandatory_items)
{
	struct asterix_cat240_record rec = summary_rec;
	int ret;

	/* Setting the flags of the always present items changes nothing. */
	rec.items |= ASTERIX_CAT240_ITEM_010 | ASTERIX_CAT240_ITEM_000;
	ret = asterix_cat240_encode(&rec, buf, sizeof(buf));
	zassert_equal(ret, sizeof(summary_block));
	zassert_mem_equal(buf, summary_block, sizeof(summary_block));
}

ZTEST(cat240_codec, test_decode_video)
{
	struct asterix_cat240_record rec;
	int ret;

	ret = asterix_cat240_decode(video_block, sizeof(video_block), &rec);
	zassert_equal(ret, sizeof(video_block), "decode returned %d", ret);
	check_video_rec(&rec, &video_block[VIDEO_BLOCK_VIDEO_OFFSET]);

	/* Trailing octets past LEN belong to the next block and are left alone. */
	memcpy(buf, video_block, sizeof(video_block));
	ret = asterix_cat240_decode(buf, sizeof(video_block) + 10, &rec);
	zassert_equal(ret, sizeof(video_block));
	check_video_rec(&rec, &buf[VIDEO_BLOCK_VIDEO_OFFSET]);
}

ZTEST(cat240_codec, test_decode_summary)
{
	struct asterix_cat240_record rec;
	int ret;

	ret = asterix_cat240_decode(summary_block, sizeof(summary_block), &rec);
	zassert_equal(ret, sizeof(summary_block), "decode returned %d", ret);
	zassert_equal(rec.items,
		      ASTERIX_CAT240_ITEM_010 | ASTERIX_CAT240_ITEM_000 | ASTERIX_CAT240_ITEM_030);
	zassert_equal(rec.sac, 0x01);
	zassert_equal(rec.sic, 0x02);
	zassert_equal(rec.msg_type, ASTERIX_CAT240_MSG_VIDEO_SUMMARY);
	zassert_equal(rec.summary_len, 3);
	zassert_equal_ptr(rec.summary, (const char *)&summary_block[SUMMARY_BLOCK_TEXT_OFFSET]);
	zassert_mem_equal(rec.summary, "PSR", 3);
	zassert_equal(asterix_cat240_video_len(&rec), 0);
}

ZTEST(cat240_codec, test_decode_record)
{
	const uint8_t *record = &video_block[ASTERIX_CAT240_BLOCK_HDR_LEN];
	size_t rec_len = sizeof(video_block) - ASTERIX_CAT240_BLOCK_HDR_LEN;
	struct asterix_cat240_record rec;
	int ret;

	ret = asterix_cat240_decode_record(record, rec_len, &rec);
	zassert_equal(ret, rec_len, "decode_record returned %d", ret);
	check_video_rec(&rec, &video_block[VIDEO_BLOCK_VIDEO_OFFSET]);

	/* A record is self delimiting: extra octets are not consumed. */
	ret = asterix_cat240_decode_record(record, rec_len + 5, &rec);
	zassert_equal(ret, rec_len);

	ret = asterix_cat240_decode_record(record, rec_len - 1, &rec);
	zassert_equal(ret, -EMSGSIZE);
}

ZTEST(cat240_codec, test_roundtrip_femto_high_re_sp)
{
	static const uint8_t re[] = {0xAA, 0xBB, 0xCC};
	static const uint8_t sp[] = {0x55};
	struct asterix_cat240_record rec = {
		.items = ASTERIX_CAT240_ITEM_020 | ASTERIX_CAT240_ITEM_041 |
			 ASTERIX_CAT240_ITEM_048 | ASTERIX_CAT240_ITEM_049 |
			 ASTERIX_CAT240_ITEM_052 | ASTERIX_CAT240_ITEM_RE | ASTERIX_CAT240_ITEM_SP,
		.sac = 0xAB,
		.sic = 0xCD,
		.msg_type = ASTERIX_CAT240_MSG_VIDEO,
		.msg_index = UINT32_MAX,
		.video_hdr = {
			.start_az = 0xFFFF,
			.end_az = 0,
			.start_rg = 0x80000000,
			.cell_dur = 1500000,
		},
		.compressed = true,
		.resolution = ASTERIX_CAT240_RES_ULTRA_HIGH,
		.nb_vb = 500,
		.nb_cells = 125,
		.video_rep = 2,
		.video = big_cells,
		.re = re,
		.re_len = sizeof(re),
		.sp = sp,
		.sp_len = sizeof(sp),
	};
	size_t video_len = 2 * ASTERIX_CAT240_VIDEO_BLOCK_HIGH_SIZE;
	struct asterix_cat240_record out;
	int ret;

	for (size_t i = 0; i < video_len; i++) {
		big_cells[i] = (uint8_t)(i * 7);
	}

	ret = asterix_cat240_encode(&rec, buf, sizeof(buf));
	zassert_equal(ret, 3 + 2 + 2 + 1 + 4 + 12 + 2 + 5 + 1 + video_len + 4 + 2,
		      "encode returned %d", ret);

	ret = asterix_cat240_decode(buf, ret, &out);
	zassert_true(ret > 0, "decode returned %d", ret);
	zassert_equal(out.items, rec.items | ASTERIX_CAT240_ITEM_010 | ASTERIX_CAT240_ITEM_000);
	zassert_equal(out.sac, rec.sac);
	zassert_equal(out.sic, rec.sic);
	zassert_equal(out.msg_type, rec.msg_type);
	zassert_equal(out.msg_index, rec.msg_index);
	zassert_mem_equal(&out.video_hdr, &rec.video_hdr, sizeof(rec.video_hdr));
	zassert_true(out.compressed);
	zassert_equal(out.resolution, rec.resolution);
	zassert_equal(out.nb_vb, rec.nb_vb);
	zassert_equal(out.nb_cells, rec.nb_cells);
	zassert_equal(out.video_rep, rec.video_rep);
	zassert_equal(asterix_cat240_video_len(&out), video_len);
	zassert_mem_equal(out.video, big_cells, rec.nb_vb);
	for (size_t i = rec.nb_vb; i < video_len; i++) {
		zassert_equal(out.video[i], 0, "octet %zu is not padding", i);
	}
	zassert_equal(out.re_len, sizeof(re));
	zassert_mem_equal(out.re, re, sizeof(re));
	zassert_equal(out.sp_len, sizeof(sp));
	zassert_mem_equal(out.sp, sp, sizeof(sp));
}

ZTEST(cat240_codec, test_roundtrip_medium_empty_re)
{
	struct asterix_cat240_record rec = video_rec;
	struct asterix_cat240_record out;
	int ret;

	rec.items &= ~(ASTERIX_CAT240_ITEM_050 | ASTERIX_CAT240_ITEM_140);
	rec.items |= ASTERIX_CAT240_ITEM_051 | ASTERIX_CAT240_ITEM_RE;
	rec.video_rep = 1;
	rec.video = big_cells;
	rec.nb_vb = ASTERIX_CAT240_VIDEO_BLOCK_MEDIUM_SIZE;
	rec.re = NULL;
	rec.re_len = 0;

	ret = asterix_cat240_encode(&rec, buf, sizeof(buf));
	zassert_equal(ret, 3 + 2 + 2 + 1 + 4 + 12 + 2 + 5 + 1 + 64 + 1, "encode returned %d", ret);

	ret = asterix_cat240_decode(buf, ret, &out);
	zassert_true(ret > 0, "decode returned %d", ret);
	zassert_equal(out.items, rec.items | ASTERIX_CAT240_ITEM_010 | ASTERIX_CAT240_ITEM_000);
	zassert_equal(asterix_cat240_video_block_size(out.items),
		      ASTERIX_CAT240_VIDEO_BLOCK_MEDIUM_SIZE);
	zassert_equal(out.video_rep, 1);
	zassert_mem_equal(out.video, big_cells, ASTERIX_CAT240_VIDEO_BLOCK_MEDIUM_SIZE);
	zassert_equal(out.re_len, 0);
	zassert_not_null(out.re);
}

ZTEST(cat240_codec, test_multi_record_block)
{
	struct asterix_cat240_record rec;
	size_t off = ASTERIX_CAT240_BLOCK_HDR_LEN;
	int block_len;
	int ret;

	ret = asterix_cat240_encode_record(&summary_rec, &buf[off], sizeof(buf) - off);
	zassert_true(ret > 0);
	off += ret;
	ret = asterix_cat240_encode_record(&video_rec, &buf[off], sizeof(buf) - off);
	zassert_true(ret > 0);
	off += ret;

	buf[0] = ASTERIX_CAT240_CAT;
	buf[1] = (uint8_t)(off >> 8);
	buf[2] = (uint8_t)off;

	/* The convenience decoder yields the first record and the whole block length. */
	ret = asterix_cat240_decode(buf, sizeof(buf), &rec);
	zassert_equal(ret, off);
	zassert_equal(rec.msg_type, ASTERIX_CAT240_MSG_VIDEO_SUMMARY);

	block_len = asterix_cat240_block_len(buf, sizeof(buf));
	zassert_equal(block_len, off);

	off = ASTERIX_CAT240_BLOCK_HDR_LEN;
	ret = asterix_cat240_decode_record(&buf[off], block_len - off, &rec);
	zassert_equal(ret, sizeof(summary_block) - ASTERIX_CAT240_BLOCK_HDR_LEN);
	zassert_equal(rec.msg_type, ASTERIX_CAT240_MSG_VIDEO_SUMMARY);
	off += ret;

	ret = asterix_cat240_decode_record(&buf[off], block_len - off, &rec);
	zassert_equal(ret, sizeof(video_block) - ASTERIX_CAT240_BLOCK_HDR_LEN);
	check_video_rec(&rec, &buf[off + VIDEO_BLOCK_VIDEO_OFFSET - ASTERIX_CAT240_BLOCK_HDR_LEN]);
	off += ret;
	zassert_equal(off, block_len);
}

ZTEST(cat240_codec, test_encode_errors)
{
	struct asterix_cat240_record rec;

	zassert_equal(asterix_cat240_encode(NULL, buf, sizeof(buf)), -EINVAL);
	zassert_equal(asterix_cat240_encode(&video_rec, NULL, sizeof(buf)), -EINVAL);
	zassert_equal(asterix_cat240_encode_record(NULL, buf, sizeof(buf)), -EINVAL);
	zassert_equal(asterix_cat240_encode_record(&video_rec, NULL, sizeof(buf)), -EINVAL);

	zassert_equal(asterix_cat240_encode(&video_rec, buf, sizeof(video_block) - 1), -ENOBUFS);
	zassert_equal(asterix_cat240_encode(&video_rec, buf, 0), -ENOBUFS);

	rec = video_rec;
	rec.msg_type = 3;
	zassert_equal(asterix_cat240_encode(&rec, buf, sizeof(buf)), -EINVAL);

	rec = video_rec;
	rec.items &= ~ASTERIX_CAT240_ITEM_020;
	zassert_equal(asterix_cat240_encode(&rec, buf, sizeof(buf)), -EINVAL);

	rec = video_rec;
	rec.items |= ASTERIX_CAT240_ITEM_041;
	zassert_equal(asterix_cat240_encode(&rec, buf, sizeof(buf)), -EINVAL);

	rec = video_rec;
	rec.items &= ~ASTERIX_CAT240_ITEM_040;
	zassert_equal(asterix_cat240_encode(&rec, buf, sizeof(buf)), -EINVAL);

	rec = video_rec;
	rec.items |= ASTERIX_CAT240_ITEM_051;
	zassert_equal(asterix_cat240_encode(&rec, buf, sizeof(buf)), -EINVAL);

	rec = video_rec;
	rec.items &= ~ASTERIX_CAT240_ITEM_050;
	zassert_equal(asterix_cat240_encode(&rec, buf, sizeof(buf)), -EINVAL);

	rec = video_rec;
	rec.items |= ASTERIX_CAT240_ITEM_030;
	rec.summary = "x";
	rec.summary_len = 1;
	zassert_equal(asterix_cat240_encode(&rec, buf, sizeof(buf)), -EINVAL);

	rec = summary_rec;
	rec.items |= ASTERIX_CAT240_ITEM_020;
	zassert_equal(asterix_cat240_encode(&rec, buf, sizeof(buf)), -EINVAL);

	rec = summary_rec;
	rec.summary_len = 0;
	zassert_equal(asterix_cat240_encode(&rec, buf, sizeof(buf)), -EINVAL);

	rec = summary_rec;
	rec.summary = NULL;
	zassert_equal(asterix_cat240_encode(&rec, buf, sizeof(buf)), -EINVAL);

	rec = video_rec;
	rec.resolution = 0;
	zassert_equal(asterix_cat240_encode(&rec, buf, sizeof(buf)), -EINVAL);
	rec.resolution = ASTERIX_CAT240_RES_ULTRA_HIGH + 1;
	zassert_equal(asterix_cat240_encode(&rec, buf, sizeof(buf)), -EINVAL);

	rec = video_rec;
	rec.nb_vb = sizeof(video_cells) + 1;
	zassert_equal(asterix_cat240_encode(&rec, buf, sizeof(buf)), -EINVAL);

	rec = video_rec;
	rec.nb_cells = ASTERIX_CAT240_NB_CELLS_MAX + 1;
	zassert_equal(asterix_cat240_encode(&rec, buf, sizeof(buf)), -EINVAL);

	rec = video_rec;
	rec.video_rep = 0;
	zassert_equal(asterix_cat240_encode(&rec, buf, sizeof(buf)), -EINVAL);

	rec = video_rec;
	rec.video = NULL;
	zassert_equal(asterix_cat240_encode(&rec, buf, sizeof(buf)), -EINVAL);

	rec = video_rec;
	rec.items = (rec.items & ~ASTERIX_CAT240_ITEM_050) | ASTERIX_CAT240_ITEM_052;
	rec.video = big_cells;
	rec.video_rep = ASTERIX_CAT240_VIDEO_BLOCK_HIGH_REP_MAX + 1;
	zassert_equal(asterix_cat240_encode(&rec, buf, sizeof(buf)), -EINVAL);

	rec = video_rec;
	rec.time_of_day = ASTERIX_CAT240_TIME_OF_DAY_MAX + 1;
	zassert_equal(asterix_cat240_encode(&rec, buf, sizeof(buf)), -EINVAL);

	rec = video_rec;
	rec.items |= ASTERIX_CAT240_ITEM_RE;
	rec.re = big_cells;
	rec.re_len = ASTERIX_CAT240_RE_SP_LEN_MAX + 1;
	zassert_equal(asterix_cat240_encode(&rec, buf, sizeof(buf)), -EINVAL);
	rec.re = NULL;
	rec.re_len = 1;
	zassert_equal(asterix_cat240_encode(&rec, buf, sizeof(buf)), -EINVAL);

	rec = video_rec;
	rec.items |= ASTERIX_CAT240_ITEM_SP;
	rec.sp = big_cells;
	rec.sp_len = ASTERIX_CAT240_RE_SP_LEN_MAX + 1;
	zassert_equal(asterix_cat240_encode(&rec, buf, sizeof(buf)), -EINVAL);
	rec.sp = NULL;
	rec.sp_len = 1;
	zassert_equal(asterix_cat240_encode(&rec, buf, sizeof(buf)), -EINVAL);
}

ZTEST(cat240_codec, test_encode_block_too_long)
{
	struct asterix_cat240_record rec = video_rec;
	int ret;

	/* The largest high data volume item plus full RE and SP fields exceed LEN. */
	rec.items = (rec.items & ~ASTERIX_CAT240_ITEM_050) | ASTERIX_CAT240_ITEM_052 |
		    ASTERIX_CAT240_ITEM_RE | ASTERIX_CAT240_ITEM_SP;
	rec.video = big_cells;
	rec.video_rep = ASTERIX_CAT240_VIDEO_BLOCK_HIGH_REP_MAX;
	rec.nb_vb = ASTERIX_CAT240_VIDEO_BLOCK_HIGH_REP_MAX * ASTERIX_CAT240_VIDEO_BLOCK_HIGH_SIZE;
	rec.re = big_cells;
	rec.re_len = ASTERIX_CAT240_RE_SP_LEN_MAX;
	rec.sp = big_cells;
	rec.sp_len = ASTERIX_CAT240_RE_SP_LEN_MAX;

	ret = asterix_cat240_encode(&rec, buf, sizeof(buf));
	zassert_equal(ret, -EMSGSIZE, "encode returned %d", ret);

	/* Without the RE and SP fields the block fits, so only the buffer is short. */
	rec.items &= ~(ASTERIX_CAT240_ITEM_RE | ASTERIX_CAT240_ITEM_SP);
	ret = asterix_cat240_encode(&rec, buf, sizeof(buf));
	zassert_equal(ret, -ENOBUFS, "encode returned %d", ret);
}

ZTEST(cat240_codec, test_decode_block_errors)
{
	struct asterix_cat240_record rec;

	zassert_equal(asterix_cat240_decode(NULL, sizeof(video_block), &rec), -EINVAL);
	zassert_equal(asterix_cat240_decode(video_block, sizeof(video_block), NULL), -EINVAL);
	zassert_equal(asterix_cat240_block_len(NULL, sizeof(video_block)), -EINVAL);
	zassert_equal(asterix_cat240_decode_record(NULL, 1, &rec), -EINVAL);
	zassert_equal(asterix_cat240_decode_record(video_block, 1, NULL), -EINVAL);

	zassert_equal(asterix_cat240_block_len(video_block, 2), -EMSGSIZE);
	zassert_equal(asterix_cat240_block_len(video_block, sizeof(video_block) - 1), -EMSGSIZE);
	zassert_equal(asterix_cat240_decode(video_block, sizeof(video_block) - 1, &rec), -EMSGSIZE);

	memcpy(buf, video_block, sizeof(video_block));
	buf[0] = 0xEF;
	zassert_equal(asterix_cat240_decode(buf, sizeof(video_block), &rec), -EBADMSG);

	/* LEN covering the header only leaves no record. */
	memcpy(buf, video_block, sizeof(video_block));
	buf[1] = 0;
	buf[2] = ASTERIX_CAT240_BLOCK_HDR_LEN;
	zassert_equal(asterix_cat240_decode(buf, sizeof(video_block), &rec), -EBADMSG);

	/* LEN cutting through the record makes the record malformed, not short. */
	memcpy(buf, video_block, sizeof(video_block));
	buf[2] = 20;
	zassert_equal(asterix_cat240_decode(buf, sizeof(video_block), &rec), -EBADMSG);
}

ZTEST(cat240_codec, test_decode_record_errors)
{
	const uint8_t *record = &video_block[ASTERIX_CAT240_BLOCK_HDR_LEN];
	size_t rec_len = sizeof(video_block) - ASTERIX_CAT240_BLOCK_HDR_LEN;
	struct asterix_cat240_record rec;

	zassert_equal(asterix_cat240_decode_record(record, 0, &rec), -EMSGSIZE);
	zassert_equal(asterix_cat240_decode_record(record, 1, &rec), -EMSGSIZE);
	zassert_equal(asterix_cat240_decode_record(record, 4, &rec), -EMSGSIZE);

	/* I240/010 missing from the FSPEC. */
	memcpy(buf, record, rec_len);
	buf[0] = 0x6B;
	zassert_equal(asterix_cat240_decode_record(buf, rec_len, &rec), -EBADMSG);

	/* Unknown message type. */
	memcpy(buf, record, rec_len);
	buf[4] = 3;
	zassert_equal(asterix_cat240_decode_record(buf, rec_len, &rec), -EBADMSG);

	/* Video message with both video headers. */
	memcpy(buf, record, rec_len);
	buf[0] |= 0x04;
	zassert_equal(asterix_cat240_decode_record(buf, rec_len, &rec), -EBADMSG);

	/* Video message without a video block. */
	memcpy(buf, record, rec_len);
	buf[1] &= ~0x40;
	zassert_equal(asterix_cat240_decode_record(buf, rec_len, &rec), -EBADMSG);

	/* Undefined resolution. */
	memcpy(buf, record, rec_len);
	buf[22] = 7;
	zassert_equal(asterix_cat240_decode_record(buf, rec_len, &rec), -EBADMSG);

	/* More valid octets than the video block holds. */
	memcpy(buf, record, rec_len);
	buf[24] = 9;
	zassert_equal(asterix_cat240_decode_record(buf, rec_len, &rec), -EBADMSG);

	/* Empty video block. */
	memcpy(buf, record, rec_len);
	buf[28] = 0;
	zassert_equal(asterix_cat240_decode_record(buf, rec_len, &rec), -EBADMSG);

	/* Video block running past the end of the input. */
	memcpy(buf, record, rec_len);
	buf[28] = 3;
	zassert_equal(asterix_cat240_decode_record(buf, rec_len, &rec), -EMSGSIZE);

	/* A third FSPEC octet is allowed only when it names no item. */
	buf[0] = record[0];
	buf[1] = record[1] | 0x01;
	buf[2] = 0x00;
	memcpy(&buf[3], &record[2], rec_len - 2);
	zassert_equal(asterix_cat240_decode_record(buf, rec_len + 1, &rec), rec_len + 1);
	check_video_rec(&rec, &buf[VIDEO_BLOCK_VIDEO_OFFSET - ASTERIX_CAT240_BLOCK_HDR_LEN + 1]);
	buf[2] = 0x80;
	zassert_equal(asterix_cat240_decode_record(buf, rec_len + 1, &rec), -EBADMSG);
	/* Extending it once more runs the FSPEC into the SAC octet. */
	buf[2] = 0x01;
	zassert_equal(asterix_cat240_decode_record(buf, rec_len + 1, &rec), -EBADMSG);

	/* Summary message carrying a video item. */
	memcpy(buf, &summary_block[ASTERIX_CAT240_BLOCK_HDR_LEN],
	       sizeof(summary_block) - ASTERIX_CAT240_BLOCK_HDR_LEN);
	buf[0] |= 0x20;
	zassert_equal(asterix_cat240_decode_record(buf, sizeof(summary_block), &rec), -EBADMSG);

	/* Empty summary. */
	memcpy(buf, &summary_block[ASTERIX_CAT240_BLOCK_HDR_LEN],
	       sizeof(summary_block) - ASTERIX_CAT240_BLOCK_HDR_LEN);
	buf[4] = 0;
	zassert_equal(asterix_cat240_decode_record(buf, sizeof(summary_block), &rec), -EBADMSG);

	/* RE field with a zero length octet. */
	buf[0] = 0xD1;
	buf[1] = 0x04;
	buf[2] = 0x01;
	buf[3] = 0x02;
	buf[4] = 0x01;
	buf[5] = 0x01;
	buf[6] = 'x';
	buf[7] = 0x00;
	zassert_equal(asterix_cat240_decode_record(buf, 8, &rec), -EBADMSG);
	buf[7] = 0x01;
	zassert_equal(asterix_cat240_decode_record(buf, 8, &rec), 8);
	zassert_equal(rec.re_len, 0);
	buf[7] = 0x03;
	zassert_equal(asterix_cat240_decode_record(buf, 8, &rec), -EMSGSIZE);
}

ZTEST(cat240_codec, test_helpers)
{
	zassert_equal(asterix_cat240_bits_per_cell(0), 0);
	zassert_equal(asterix_cat240_bits_per_cell(ASTERIX_CAT240_RES_MONOBIT), 1);
	zassert_equal(asterix_cat240_bits_per_cell(ASTERIX_CAT240_RES_LOW), 2);
	zassert_equal(asterix_cat240_bits_per_cell(ASTERIX_CAT240_RES_MEDIUM), 4);
	zassert_equal(asterix_cat240_bits_per_cell(ASTERIX_CAT240_RES_HIGH), 8);
	zassert_equal(asterix_cat240_bits_per_cell(ASTERIX_CAT240_RES_VERY_HIGH), 16);
	zassert_equal(asterix_cat240_bits_per_cell(ASTERIX_CAT240_RES_ULTRA_HIGH), 32);
	zassert_equal(asterix_cat240_bits_per_cell(ASTERIX_CAT240_RES_ULTRA_HIGH + 1), 0);

	zassert_equal(asterix_cat240_video_block_size(0), 0);
	zassert_equal(asterix_cat240_video_block_size(ASTERIX_CAT240_ITEM_050),
		      ASTERIX_CAT240_VIDEO_BLOCK_LOW_SIZE);
	zassert_equal(asterix_cat240_video_block_size(ASTERIX_CAT240_ITEM_051),
		      ASTERIX_CAT240_VIDEO_BLOCK_MEDIUM_SIZE);
	zassert_equal(asterix_cat240_video_block_size(ASTERIX_CAT240_ITEM_052),
		      ASTERIX_CAT240_VIDEO_BLOCK_HIGH_SIZE);

	zassert_equal(asterix_cat240_video_len(NULL), 0);
	zassert_equal(asterix_cat240_video_len(&video_rec), sizeof(video_cells));
}

ZTEST_SUITE(cat240_codec, NULL, NULL, NULL, NULL, NULL);
