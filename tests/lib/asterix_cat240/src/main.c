/*
 * SPDX-FileCopyrightText: 2026 Muhammad Waleed Badar
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <errno.h>
#include <string.h>

#if defined(__ZEPHYR__)
#include <zephyr/posix/arpa/inet.h>
#include <zephyr/posix/net/if.h>
#include <zephyr/posix/netinet/in.h>
#include <zephyr/posix/sys/socket.h>
#include <zephyr/posix/unistd.h>
#else
#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

#include <zephyr/ztest.h>

#include <asterix/cat240.h>

#include "cat240_codec.h"

#define RX_PORT 8600
#define SAC     0x12
#define SIC     0x34

static const struct asterix_cat240_config config = {
	.addr = "127.0.0.1",
	.port = RX_PORT,
	.sac = SAC,
	.sic = SIC,
};

static struct asterix_cat240 ctx;
static int rx_sock = -1;
static uint8_t rx_buf[ASTERIX_CAT240_TX_BUF_SIZE];
static uint8_t cells[2048];

static void *suite_setup(void)
{
	struct sockaddr_in addr = {
		.sin_family = AF_INET,
		.sin_port = htons(RX_PORT),
	};
	struct timeval timeout = {.tv_sec = 2};
	int ret;

	for (size_t i = 0; i < sizeof(cells); i++) {
		cells[i] = (uint8_t)(i * 13U);
	}

	rx_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	zassert_true(rx_sock >= 0, "socket failed (%d)", errno);

	ret = setsockopt(rx_sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
	zassert_equal(ret, 0, "setsockopt failed (%d)", errno);

	ret = inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
	zassert_equal(ret, 1);

	ret = bind(rx_sock, (struct sockaddr *)&addr, sizeof(addr));
	zassert_equal(ret, 0, "bind failed (%d)", errno);

	return NULL;
}

static void suite_teardown(void *fixture)
{
	ARG_UNUSED(fixture);

	(void)close(rx_sock);
}

static void test_before(void *fixture)
{
	int ret;

	ARG_UNUSED(fixture);

	/* Drop whatever an earlier test sent and did not read. */
	while (recv(rx_sock, rx_buf, sizeof(rx_buf), MSG_DONTWAIT) > 0) {
	}

	ret = asterix_cat240_init(&ctx, &config);
	zassert_equal(ret, 0, "init returned %d", ret);
}

static void test_after(void *fixture)
{
	ARG_UNUSED(fixture);

	(void)asterix_cat240_close(&ctx);
}

/* Receives one datagram and decodes it, returning the datagram length. */
static int receive(struct asterix_cat240_record *rec)
{
	int len;
	int ret;

	len = (int)recv(rx_sock, rx_buf, sizeof(rx_buf), 0);
	zassert_true(len > 0, "recv failed (%d)", errno);

	ret = asterix_cat240_decode(rx_buf, len, rec);
	zassert_equal(ret, len, "decode returned %d for %d octets", ret, len);

	zassert_equal(rec->sac, SAC);
	zassert_equal(rec->sic, SIC);
	zassert_equal((rec->items & ASTERIX_CAT240_ITEM_140) != 0U,
		      ASTERIX_CAT240_TIME_OF_DAY != 0);

	return len;
}

ZTEST(asterix_cat240, test_send_summary)
{
	static const char text[] = "PSR video";
	struct asterix_cat240_record rec;
	int ret;

	ret = asterix_cat240_send_summary(&ctx, text);
	zassert_true(ret > 0, "send_summary returned %d", ret);
	zassert_equal(receive(&rec), ret);

	zassert_equal(rec.msg_type, ASTERIX_CAT240_MSG_VIDEO_SUMMARY);
	zassert_equal(rec.summary_len, strlen(text));
	zassert_mem_equal(rec.summary, text, strlen(text));
}

ZTEST(asterix_cat240, test_send_video_low)
{
	struct asterix_cat240_video video = {
		.start_az = 0x1000,
		.end_az = 0x1100,
		.start_rg = 5,
		.cell_dur = 200,
		.resolution = ASTERIX_CAT240_RES_HIGH,
		.cells = cells,
		.len = 256,
	};
	struct asterix_cat240_record rec;
	int ret;

	for (uint32_t i = 0; i < 3; i++) {
		ret = asterix_cat240_send_video(&ctx, &video);
		zassert_true(ret > 0, "send_video returned %d", ret);
		zassert_equal(receive(&rec), ret);

		zassert_equal(rec.msg_type, ASTERIX_CAT240_MSG_VIDEO);
		zassert_equal(rec.msg_index, i);
		zassert_true((rec.items & ASTERIX_CAT240_ITEM_040) != 0U);
		zassert_true((rec.items & ASTERIX_CAT240_ITEM_050) != 0U);
		zassert_equal(rec.video_hdr.start_az, 0x1000);
		zassert_equal(rec.video_hdr.end_az, 0x1100);
		zassert_equal(rec.video_hdr.start_rg, 5);
		zassert_equal(rec.video_hdr.cell_dur, 200);
		zassert_false(rec.compressed);
		zassert_equal(rec.resolution, ASTERIX_CAT240_RES_HIGH);
		zassert_equal(rec.nb_vb, 256);
		zassert_equal(rec.nb_cells, 256);
		zassert_equal(rec.video_rep, 64);
		zassert_mem_equal(rec.video, cells, 256);
	}
}

ZTEST(asterix_cat240, test_send_video_medium_padded)
{
	struct asterix_cat240_video video = {
		.cell_dur = 1500000,
		.femto = true,
		.resolution = ASTERIX_CAT240_RES_MEDIUM,
		.compressed = true,
		.cells = cells,
		.len = 1021,
	};
	struct asterix_cat240_record rec;
	int ret;

	ret = asterix_cat240_send_video(&ctx, &video);
	zassert_true(ret > 0, "send_video returned %d", ret);
	zassert_equal(receive(&rec), ret);

	zassert_true((rec.items & ASTERIX_CAT240_ITEM_041) != 0U);
	zassert_true((rec.items & ASTERIX_CAT240_ITEM_051) != 0U);
	zassert_equal(rec.video_hdr.cell_dur, 1500000);
	zassert_true(rec.compressed);
	zassert_equal(rec.nb_vb, 1021);
	zassert_equal(rec.nb_cells, 2042);
	zassert_equal(rec.video_rep, 16);
	zassert_equal(asterix_cat240_video_len(&rec), 1024);
	zassert_mem_equal(rec.video, cells, 1021);
	zassert_equal(rec.video[1021], 0);
	zassert_equal(rec.video[1022], 0);
	zassert_equal(rec.video[1023], 0);
}

ZTEST(asterix_cat240, test_send_video_cell_count)
{
	struct asterix_cat240_video video = {
		.cell_dur = 100,
		.resolution = ASTERIX_CAT240_RES_MONOBIT,
		.cells = cells,
		.len = 38,
		.nb_cells = 300,
	};
	struct asterix_cat240_record rec;
	int ret;

	ret = asterix_cat240_send_video(&ctx, &video);
	zassert_true(ret > 0, "send_video returned %d", ret);
	zassert_equal(receive(&rec), ret);
	zassert_equal(rec.nb_vb, 38);
	zassert_equal(rec.nb_cells, 300);
	zassert_equal(rec.video_rep, 10);

	/* 305 monobit cells do not fit 38 octets. */
	video.nb_cells = 305;
	zassert_equal(asterix_cat240_send_video(&ctx, &video), -EINVAL);
}

ZTEST(asterix_cat240, test_send_errors)
{
	struct asterix_cat240_video video = {
		.cell_dur = 100,
		.resolution = ASTERIX_CAT240_RES_HIGH,
		.cells = cells,
		.len = 256,
	};
	struct asterix_cat240 closed = {0};
	struct asterix_cat240_record rec;
	char long_text[UINT8_MAX + 2];
	int ret;

	zassert_equal(asterix_cat240_send_video(NULL, &video), -EINVAL);
	zassert_equal(asterix_cat240_send_video(&closed, &video), -EINVAL);
	zassert_equal(asterix_cat240_send_video(&ctx, NULL), -EINVAL);

	video.cells = NULL;
	zassert_equal(asterix_cat240_send_video(&ctx, &video), -EINVAL);
	video.cells = cells;

	video.len = 0;
	zassert_equal(asterix_cat240_send_video(&ctx, &video), -EINVAL);
	video.len = 256;

	video.resolution = 0;
	zassert_equal(asterix_cat240_send_video(&ctx, &video), -EINVAL);
	video.resolution = ASTERIX_CAT240_RES_ULTRA_HIGH + 1;
	zassert_equal(asterix_cat240_send_video(&ctx, &video), -EINVAL);
	video.resolution = ASTERIX_CAT240_RES_HIGH;

	/* Longer than the transmit buffer, and longer than any video block item. */
	video.len = ASTERIX_CAT240_TX_BUF_SIZE;
	zassert_equal(asterix_cat240_send_video(&ctx, &video), -EMSGSIZE);
	video.len = 65025;
	zassert_equal(asterix_cat240_send_video(&ctx, &video), -EMSGSIZE);

	zassert_equal(asterix_cat240_send_summary(NULL, "x"), -EINVAL);
	zassert_equal(asterix_cat240_send_summary(&closed, "x"), -EINVAL);
	zassert_equal(asterix_cat240_send_summary(&ctx, NULL), -EINVAL);
	zassert_equal(asterix_cat240_send_summary(&ctx, ""), -EINVAL);
	memset(long_text, 'a', sizeof(long_text) - 1);
	long_text[sizeof(long_text) - 1] = '\0';
	zassert_equal(asterix_cat240_send_summary(&ctx, long_text), -EINVAL);
	long_text[UINT8_MAX] = '\0';
	ret = asterix_cat240_send_summary(&ctx, long_text);
	zassert_true(ret > 0, "send_summary returned %d", ret);
	zassert_equal(receive(&rec), ret);
	zassert_equal(rec.summary_len, UINT8_MAX);
}

/* 16 by 16 image whose pixel value is its column plus 16 times its row. */
#define IMAGE_SIZE 16
static uint8_t image_pixels[IMAGE_SIZE * IMAGE_SIZE];
static uint8_t radial_buf[32];

ZTEST(asterix_cat240, test_send_cartesian_video)
{
	struct asterix_cat240_cartesian_video image = {
		.pixels = image_pixels,
		.width = IMAGE_SIZE,
		.height = IMAGE_SIZE,
		.center_x = 8,
		.center_y = 8,
		.pixel_dur = 66713000,
		.femto = true,
		.radials = 4,
		.radial = radial_buf,
		.radial_size = sizeof(radial_buf),
	};
	/* Up, right, down and left from the center pixel, which is 8 + 16 * 8. */
	static const struct {
		uint8_t len;
		int step;
	} expected[4] = {{9, -16}, {8, 1}, {8, 16}, {9, -1}};
	struct asterix_cat240_record rec;
	int ret;

	for (size_t i = 0; i < sizeof(image_pixels); i++) {
		image_pixels[i] = (uint8_t)i;
	}

	ret = asterix_cat240_send_cartesian_video(&ctx, &image);
	zassert_equal(ret, 4, "send_cartesian_video returned %d", ret);

	for (uint32_t i = 0; i < 4; i++) {
		receive(&rec);
		zassert_equal(rec.msg_index, i);
		zassert_equal(rec.video_hdr.start_az, i * 16384);
		zassert_equal(rec.video_hdr.end_az, (uint16_t)((i + 1) * 16384));
		zassert_equal(rec.video_hdr.start_rg, 0);
		zassert_equal(rec.video_hdr.cell_dur, 66713000);
		zassert_true((rec.items & ASTERIX_CAT240_ITEM_041) != 0U);
		zassert_equal(rec.resolution, ASTERIX_CAT240_RES_HIGH);
		zassert_equal(rec.nb_vb, expected[i].len, "radial %u holds %u cells", i, rec.nb_vb);
		zassert_equal(rec.nb_cells, expected[i].len);
		for (int k = 0; k < expected[i].len; k++) {
			zassert_equal(rec.video[k], (uint8_t)(136 + k * expected[i].step),
				      "radial %u cell %d is %u", i, k, rec.video[k]);
		}
	}
}

ZTEST(asterix_cat240, test_send_cartesian_video_errors)
{
	struct asterix_cat240_cartesian_video image = {
		.pixels = image_pixels,
		.width = IMAGE_SIZE,
		.height = IMAGE_SIZE,
		.center_x = 8,
		.center_y = 8,
		.pixel_dur = 100,
		.radials = 4,
		.radial = radial_buf,
		.radial_size = sizeof(radial_buf),
	};
	struct asterix_cat240_cartesian_video bad;
	struct asterix_cat240 closed = {0};

	zassert_equal(asterix_cat240_send_cartesian_video(NULL, &image), -EINVAL);
	zassert_equal(asterix_cat240_send_cartesian_video(&closed, &image), -EINVAL);
	zassert_equal(asterix_cat240_send_cartesian_video(&ctx, NULL), -EINVAL);

	bad = image;
	bad.pixels = NULL;
	zassert_equal(asterix_cat240_send_cartesian_video(&ctx, &bad), -EINVAL);
	bad = image;
	bad.radial = NULL;
	zassert_equal(asterix_cat240_send_cartesian_video(&ctx, &bad), -EINVAL);
	bad = image;
	bad.width = 0;
	zassert_equal(asterix_cat240_send_cartesian_video(&ctx, &bad), -EINVAL);
	bad = image;
	bad.center_y = IMAGE_SIZE;
	zassert_equal(asterix_cat240_send_cartesian_video(&ctx, &bad), -EINVAL);
	bad = image;
	bad.pixel_dur = 0;
	zassert_equal(asterix_cat240_send_cartesian_video(&ctx, &bad), -EINVAL);
	bad = image;
	bad.radials = 0;
	zassert_equal(asterix_cat240_send_cartesian_video(&ctx, &bad), -EINVAL);

	/* The radial pointing up holds nine pixels. */
	bad = image;
	bad.radial_size = 8;
	zassert_equal(asterix_cat240_send_cartesian_video(&ctx, &bad), -EMSGSIZE);
}

ZTEST(asterix_cat240, test_init_errors)
{
	struct asterix_cat240_config cfg = config;
	struct asterix_cat240 other;
	int ret;

	zassert_equal(asterix_cat240_init(NULL, &cfg), -EINVAL);
	zassert_equal(asterix_cat240_init(&other, NULL), -EINVAL);

	cfg.port = 0;
	zassert_equal(asterix_cat240_init(&other, &cfg), -EINVAL);
	cfg.port = RX_PORT;

	cfg.addr = NULL;
	zassert_equal(asterix_cat240_init(&other, &cfg), -EINVAL);
	cfg.addr = "radar";
	zassert_equal(asterix_cat240_init(&other, &cfg), -EINVAL);

	/* IPv6 works where it is enabled and fails cleanly where it is not. */
	cfg.addr = "::1";
	ret = asterix_cat240_init(&other, &cfg);
	zassert_true(ret <= 0, "init returned %d", ret);
	if (ret == 0) {
		zassert_equal(asterix_cat240_close(&other), 0);
	}
}

ZTEST(asterix_cat240, test_close)
{
	zassert_equal(asterix_cat240_close(NULL), -EINVAL);
	zassert_equal(asterix_cat240_close(&ctx), 0);
	zassert_equal(asterix_cat240_close(&ctx), -EINVAL);
	zassert_equal(asterix_cat240_send_summary(&ctx, "x"), -EINVAL);
}

ZTEST(asterix_cat240, test_bind_iface)
{
	struct asterix_cat240_config cfg = config;
	struct asterix_cat240 other;
	struct asterix_cat240_record rec;
	char loopback[IF_NAMESIZE];
	int ret;

	/* The loopback is the only, hence the first, interface of this test. */
	zassert_not_null(if_indextoname(1, loopback));

	cfg.iface = loopback;
	ret = asterix_cat240_init(&other, &cfg);
	zassert_equal(ret, 0, "init returned %d", ret);
	ret = asterix_cat240_send_summary(&other, "bound");
	zassert_true(ret > 0, "send_summary returned %d", ret);
	zassert_equal(receive(&rec), ret);
	zassert_equal(asterix_cat240_close(&other), 0);

	cfg.iface = "nosuch0";
	zassert_equal(asterix_cat240_init(&other, &cfg), -ENODEV);

	cfg.iface = "a-name-far-too-long-for-any-interface";
	zassert_equal(asterix_cat240_init(&other, &cfg), -EINVAL);

	/* An empty name means no binding. */
	cfg.iface = "";
	zassert_equal(asterix_cat240_init(&other, &cfg), 0);
	zassert_equal(asterix_cat240_close(&other), 0);
}

ZTEST(asterix_cat240, test_multicast_init)
{
	struct asterix_cat240_config cfg = config;
	struct asterix_cat240 other;
	int ret;

	cfg.addr = "239.192.0.240";
	ret = asterix_cat240_init(&other, &cfg);
	zassert_equal(ret, 0, "init returned %d", ret);
	zassert_equal(asterix_cat240_close(&other), 0);
}

ZTEST_SUITE(asterix_cat240, NULL, suite_setup, test_before, test_after, suite_teardown);
