/*
 * SPDX-FileCopyrightText: 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

/*
 * ASTERIX Category 240 message codec: the data block and record layout, the
 * standard User Application Profile (UAP) and all data items of the category.
 * Internal to the library; the public API is <asterix/cat240.h>.
 */

#ifndef ASTERIX_CAT240_CODEC_H_
#define ASTERIX_CAT240_CODEC_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <asterix/cat240.h>

#include "cat240_port.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * A data block is one CAT octet, a two octet big endian length LEN covering
 * the whole block, and one or more records. A record is a Field
 * Specification (FSPEC) naming the data items present, followed by the items
 * in Field Reference Number (FRN) order.
 */

/** Data category code carried in the first octet of every data block. */
#define ASTERIX_CAT240_CAT 240U

/** Length of the data block header: CAT (one octet) and LEN (two octets). */
#define ASTERIX_CAT240_BLOCK_HDR_LEN 3U

/** Message types carried in I240/000. */
enum asterix_cat240_msg_type {
	/** Video summary message: stream meta data as free text. */
	ASTERIX_CAT240_MSG_VIDEO_SUMMARY = 1,
	/** Video message: one radial of video cells. */
	ASTERIX_CAT240_MSG_VIDEO = 2,
};

/**
 * @name Data item presence flags
 *
 * One flag per field of the standard UAP, in FRN order, ORed together in
 * @ref asterix_cat240_record.items.
 * @{
 */
/** I240/010 Data Source Identifier, FRN 1. */
#define ASTERIX_CAT240_ITEM_010 CAT240_BIT(0)
/** I240/000 Message Type, FRN 2. */
#define ASTERIX_CAT240_ITEM_000 CAT240_BIT(1)
/** I240/020 Video Record Header, FRN 3. */
#define ASTERIX_CAT240_ITEM_020 CAT240_BIT(2)
/** I240/030 Video Summary, FRN 4. */
#define ASTERIX_CAT240_ITEM_030 CAT240_BIT(3)
/** I240/040 Video Header Nano, FRN 5. */
#define ASTERIX_CAT240_ITEM_040 CAT240_BIT(4)
/** I240/041 Video Header Femto, FRN 6. */
#define ASTERIX_CAT240_ITEM_041 CAT240_BIT(5)
/** I240/048 Video Cells Resolution and Data Compression Indicator, FRN 7. */
#define ASTERIX_CAT240_ITEM_048 CAT240_BIT(6)
/** I240/049 Video Octets and Video Cells Counters, FRN 8. */
#define ASTERIX_CAT240_ITEM_049 CAT240_BIT(7)
/** I240/050 Video Block Low Data Volume, FRN 9. */
#define ASTERIX_CAT240_ITEM_050 CAT240_BIT(8)
/** I240/051 Video Block Medium Data Volume, FRN 10. */
#define ASTERIX_CAT240_ITEM_051 CAT240_BIT(9)
/** I240/052 Video Block High Data Volume, FRN 11. */
#define ASTERIX_CAT240_ITEM_052 CAT240_BIT(10)
/** I240/140 Time of Day, FRN 12. */
#define ASTERIX_CAT240_ITEM_140 CAT240_BIT(11)
/** Reserved Expansion Field, FRN 13. */
#define ASTERIX_CAT240_ITEM_RE  CAT240_BIT(12)
/** Special Purpose Field, FRN 14. */
#define ASTERIX_CAT240_ITEM_SP  CAT240_BIT(13)
/** Mask of the video block items, of which a video message carries exactly one. */
#define ASTERIX_CAT240_ITEM_VIDEO_BLOCK_MASK                                                       \
	(ASTERIX_CAT240_ITEM_050 | ASTERIX_CAT240_ITEM_051 | ASTERIX_CAT240_ITEM_052)
/** @} */

/** Size in octets of one video block of I240/050. */
#define ASTERIX_CAT240_VIDEO_BLOCK_LOW_SIZE     4U
/** Size in octets of one video block of I240/051. */
#define ASTERIX_CAT240_VIDEO_BLOCK_MEDIUM_SIZE  64U
/** Size in octets of one video block of I240/052. */
#define ASTERIX_CAT240_VIDEO_BLOCK_HIGH_SIZE    256U
/** Largest repetition factor of I240/052, which keeps the item below 64 KiB. */
#define ASTERIX_CAT240_VIDEO_BLOCK_HIGH_REP_MAX 254U

/** Time of day ticks per second: the LSB of I240/140 is 1/128 s. */
#define ASTERIX_CAT240_TIME_OF_DAY_HZ  128U
/** Largest time of day value: the item is three octets wide. */
#define ASTERIX_CAT240_TIME_OF_DAY_MAX 0xFFFFFFU
/** Largest number of valid cells: the NB_CELLS field is 24 bits wide. */
#define ASTERIX_CAT240_NB_CELLS_MAX    0xFFFFFFU
/** Largest payload of a Reserved Expansion or Special Purpose field. */
#define ASTERIX_CAT240_RE_SP_LEN_MAX   254U

/**
 * @brief Video header, I240/040 (nano) or I240/041 (femto)
 *
 * Describes a group of video cells that are consecutive in range and share
 * the same size in azimuth and range. The range in meters of the cell at
 * position n in the video block, counted from 1, is
 * cell_dur * (start_rg + n - 1) * c / 2 with cell_dur in seconds and
 * c = 299792458 m/s.
 */
struct asterix_cat240_video_header {
	/** Start azimuth of the cell group, LSB 360/65536 degrees. */
	uint16_t start_az;
	/** End azimuth of the cell group, LSB 360/65536 degrees. */
	uint16_t end_az;
	/** Starting range of the cell group in cells, 0 being the radar location. */
	uint32_t start_rg;
	/** Cell duration in nanoseconds (I240/040) or femtoseconds (I240/041). */
	uint32_t cell_dur;
};

/**
 * @brief One Category 240 record
 *
 * @ref items names the data items present. A field is only meaningful when
 * the flag of its item is set. Pointers of a decoded record refer into the
 * decoded buffer and stay valid as long as that buffer does.
 */
struct asterix_cat240_record {
	/** Data items present, ORed ``ASTERIX_CAT240_ITEM_*`` flags. */
	uint16_t items;
	/** I240/010: System Area Code. */
	uint8_t sac;
	/** I240/010: System Identification Code. */
	uint8_t sic;
	/** I240/000: message type, see @ref asterix_cat240_msg_type. */
	uint8_t msg_type;
	/** I240/020: message sequence identifier, a cyclic counter. */
	uint32_t msg_index;
	/** I240/030: ASCII stream meta data, not NUL terminated. */
	const char *summary;
	/** I240/030: number of characters at @ref summary, at least one. */
	uint8_t summary_len;
	/** I240/040 or I240/041: the video header. */
	struct asterix_cat240_video_header video_hdr;
	/** I240/048: a data compression technique has been applied. */
	bool compressed;
	/** I240/048: cell amplitude resolution, see @ref asterix_cat240_resolution. */
	uint8_t resolution;
	/** I240/049: number of valid octets in the video block item. */
	uint16_t nb_vb;
	/** I240/049: number of valid cells in the video block item, 24 bits. */
	uint32_t nb_cells;
	/** I240/050, I240/051 or I240/052: repetition factor, at least one. */
	uint8_t video_rep;
	/**
	 * I240/050, I240/051 or I240/052: video cell amplitudes, @ref video_rep
	 * blocks of the item's block size. The first cell is the closest to the
	 * sensor. The encoder reads only the first @ref nb_vb octets and writes
	 * the rest of the last block as zero padding.
	 */
	const uint8_t *video;
	/** I240/140: time of day in 1/128 s since midnight UTC, 24 bits. */
	uint32_t time_of_day;
	/** Reserved Expansion Field payload, without its length octet. */
	const uint8_t *re;
	/** Length of the Reserved Expansion Field payload. */
	uint8_t re_len;
	/** Special Purpose Field payload, without its length octet. */
	const uint8_t *sp;
	/** Length of the Special Purpose Field payload. */
	uint8_t sp_len;
};

/**
 * @brief Get the size of one video block of the video block item present
 *
 * @param items Data item presence flags of a record.
 *
 * @return Size in octets of one block of the I240/050, I240/051 or I240/052
 *         item present, or 0 when none of them is.
 */
size_t asterix_cat240_video_block_size(uint16_t items);

/**
 * @brief Get the length of the video data of a record
 *
 * @param rec Record.
 *
 * @return Number of octets at @ref asterix_cat240_record.video, that is the
 *         repetition factor times the block size, or 0 without a video block
 *         item.
 */
size_t asterix_cat240_video_len(const struct asterix_cat240_record *rec);

/**
 * @brief Encode one record
 *
 * Writes the FSPEC and the data items of @p rec in FRN order. I240/010 and
 * I240/000 are mandatory in every message and are always encoded, whether
 * or not their flags are set in @ref asterix_cat240_record.items. The other
 * items must form a valid message for the message type: a video summary
 * message carries I240/030 and none of the video items, a video message
 * carries I240/020, I240/048, I240/049, one of I240/040 and I240/041, and one
 * of I240/050, I240/051 and I240/052. I240/140, RE and SP are optional in
 * both.
 *
 * @param rec Record to encode.
 * @param buf Output buffer.
 * @param len Size of @p buf in octets.
 *
 * @retval >0 Number of octets written.
 * @retval -EINVAL @p rec or @p buf is NULL, or @p rec does not describe a
 *         valid message.
 * @retval -ENOBUFS @p buf is too small.
 */
int asterix_cat240_encode_record(const struct asterix_cat240_record *rec, uint8_t *buf, size_t len);

/**
 * @brief Encode a data block holding one record
 *
 * Writes the CAT and LEN octets followed by the record, as
 * asterix_cat240_encode_record() does. A data block holding several records
 * is assembled by the caller from asterix_cat240_encode_record() output.
 *
 * @param rec Record to encode.
 * @param buf Output buffer.
 * @param len Size of @p buf in octets.
 *
 * @retval >0 Length of the data block written.
 * @retval -EINVAL @p rec or @p buf is NULL, or @p rec does not describe a
 *         valid message.
 * @retval -ENOBUFS @p buf is too small.
 * @retval -EMSGSIZE The data block would not fit the 16 bit LEN field.
 */
int asterix_cat240_encode(const struct asterix_cat240_record *rec, uint8_t *buf, size_t len);

/**
 * @brief Validate a data block header
 *
 * Checks the CAT octet and the LEN field against the octets available.
 *
 * @param buf Start of a data block.
 * @param len Number of octets available at @p buf.
 *
 * @retval >0 Length of the data block, CAT and LEN included.
 * @retval -EINVAL @p buf is NULL.
 * @retval -EBADMSG @p buf does not start a Category 240 data block, or LEN
 *         leaves no room for a record.
 * @retval -EMSGSIZE @p len is shorter than the data block.
 */
int asterix_cat240_block_len(const uint8_t *buf, size_t len);

/**
 * @brief Decode one record
 *
 * Parses the FSPEC and the data items that follow it into @p rec. The item
 * set is checked against the message type as asterix_cat240_encode_record()
 * describes. Pointers in @p rec refer into @p buf.
 *
 * @param buf Start of a record, that is of its FSPEC.
 * @param len Number of octets available at @p buf.
 * @param rec Decoded record.
 *
 * @retval >0 Number of octets the record occupies.
 * @retval -EINVAL @p buf or @p rec is NULL.
 * @retval -EBADMSG The record is malformed, names a data item outside the
 *         standard UAP, or does not form a valid message.
 * @retval -EMSGSIZE @p len is shorter than the record.
 */
int asterix_cat240_decode_record(const uint8_t *buf, size_t len, struct asterix_cat240_record *rec);

/**
 * @brief Decode the first record of a data block
 *
 * Validates the data block header as asterix_cat240_block_len() does and
 * decodes the first record as asterix_cat240_decode_record() does. The
 * records that may follow it in the same data block are reached with those
 * two functions.
 *
 * @param buf Start of a data block.
 * @param len Number of octets available at @p buf.
 * @param rec Decoded record.
 *
 * @retval >0 Length of the data block, CAT and LEN included.
 * @retval -EINVAL @p buf or @p rec is NULL.
 * @retval -EBADMSG The data block or its first record is malformed.
 * @retval -EMSGSIZE @p len is shorter than the data block.
 */
int asterix_cat240_decode(const uint8_t *buf, size_t len, struct asterix_cat240_record *rec);

#ifdef __cplusplus
}
#endif

#endif /* ASTERIX_CAT240_CODEC_H_ */
