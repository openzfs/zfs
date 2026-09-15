// SPDX-License-Identifier: CDDL-1.0
/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * https://opensource.org/license/CDDL-1.0.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/abd.h>
#include <sys/bitops.h>
#include <sys/zio_compress.h>
#include <sys/zstd/zstd.h>

#include <libspl.h>
#include <libzpool.h>

#define	TEST_SIZE (128 * 1024)

int
main(void)
{
	uint8_t *plain = NULL;
	uint8_t *compressed = NULL;
	uint8_t *decoded = NULL;
	abd_t *plain_abd = NULL;
	abd_t *compressed_abd = NULL;
	abd_t *decoded_abd = NULL;
	zfs_zstdhdr_t *header;
	size_t compressed_len;
	size_t payload_len;
	boolean_t spl_ready = B_FALSE;
	boolean_t abd_ready = B_FALSE;
	boolean_t zstd_ready = B_FALSE;
	int rc = EXIT_FAILURE;

	plain = malloc(TEST_SIZE);
	compressed = malloc(TEST_SIZE);
	decoded = malloc(TEST_SIZE);
	if (plain == NULL || compressed == NULL || decoded == NULL) {
		(void) fprintf(stderr, "zstd_dctx_test: allocation failed\n");
		goto out;
	}

	for (size_t i = 0; i < TEST_SIZE; i++)
		plain[i] = (uint8_t)(i / 1024);

	libspl_init();
	spl_ready = B_TRUE;
	abd_init();
	abd_ready = B_TRUE;
	if (zstd_init() != 0) {
		(void) fprintf(stderr, "zstd_dctx_test: zstd_init failed\n");
		goto out;
	}
	zstd_ready = B_TRUE;

	plain_abd = abd_get_from_buf(plain, TEST_SIZE);
	compressed_abd = abd_get_from_buf(compressed, TEST_SIZE);
	decoded_abd = abd_get_from_buf(decoded, TEST_SIZE);
	if (plain_abd == NULL || compressed_abd == NULL ||
	    decoded_abd == NULL) {
		(void) fprintf(stderr,
		    "zstd_dctx_test: ABD allocation failed\n");
		goto out;
	}

	compressed_len = zfs_zstd_compress(plain_abd, compressed_abd,
	    TEST_SIZE, TEST_SIZE, ZIO_ZSTD_LEVEL_1);
	if (compressed_len >= TEST_SIZE ||
	    compressed_len <= sizeof (zfs_zstdhdr_t)) {
		(void) fprintf(stderr,
		    "zstd_dctx_test: test data did not compress\n");
		goto out;
	}

	header = (zfs_zstdhdr_t *)compressed;
	payload_len = compressed_len - sizeof (*header);

	/* Shorten the declared frame to force a ZSTD decoder error. */
	header->c_len = BE_32(payload_len - 1);
	if (zfs_zstd_decompress(compressed_abd, decoded_abd, compressed_len,
	    TEST_SIZE, 0) == 0) {
		(void) fprintf(stderr,
		    "zstd_dctx_test: truncated frame unexpectedly succeeded\n");
		goto out;
	}

	/* The same cached context must still decode a complete frame. */
	header->c_len = BE_32(payload_len);
	if (zfs_zstd_decompress(compressed_abd, decoded_abd, compressed_len,
	    TEST_SIZE, 0) != 0 || memcmp(plain, decoded, TEST_SIZE) != 0) {
		(void) fprintf(stderr,
		    "zstd_dctx_test: valid frame failed after decoder error\n");
		goto out;
	}

	rc = EXIT_SUCCESS;
out:
	if (plain_abd != NULL)
		abd_free(plain_abd);
	if (compressed_abd != NULL)
		abd_free(compressed_abd);
	if (decoded_abd != NULL)
		abd_free(decoded_abd);
	if (zstd_ready)
		zstd_fini();
	if (abd_ready)
		abd_fini();
	if (spl_ready)
		libspl_fini();
	free(plain);
	free(compressed);
	free(decoded);
	return (rc);
}
