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
/*
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include <sys/zfs_context.h>
#include <modes/modes.h>
#include <sys/crypto/common.h>
#include <sys/crypto/impl.h>

/*
 * Utility routine to copy a buffer to a crypto_data structure.
 */

/*
 * Utility routine to apply the command COPY_TO_DATA to the
 * data in the uio structure.
 */
static int
crypto_uio_copy_to_data(crypto_data_t *data, uchar_t *buf, int len)
{
	zfs_uio_t *uiop = data->cd_uio;
	off_t offset = data->cd_offset;
	size_t length = len;
	uint_t vec_idx;
	size_t cur_len;
	uchar_t *datap;

	ASSERT(data->cd_format == CRYPTO_DATA_UIO);
	if (zfs_uio_segflg(uiop) != UIO_SYSSPACE) {
		return (CRYPTO_ARGUMENTS_BAD);
	}

	/*
	 * Jump to the first iovec containing data to be
	 * processed.
	 */
	offset = zfs_uio_index_at_offset(uiop, offset, &vec_idx);

	if (vec_idx == zfs_uio_iovcnt(uiop) && length > 0) {
		/*
		 * The caller specified an offset that is larger than
		 * the total size of the buffers it provided.
		 */
		return (CRYPTO_DATA_LEN_RANGE);
	}

	while (vec_idx < zfs_uio_iovcnt(uiop) && length > 0) {
		cur_len = MIN(zfs_uio_iovlen(uiop, vec_idx) -
		    offset, length);

		datap = (uchar_t *)(zfs_uio_iovbase(uiop, vec_idx) + offset);
		memcpy(datap, buf, cur_len);
		buf += cur_len;

		length -= cur_len;
		vec_idx++;
		offset = 0;
	}

	if (vec_idx == zfs_uio_iovcnt(uiop) && length > 0) {
		/*
		 * The end of the specified iovecs was reached but
		 * the length requested could not be processed.
		 */
		data->cd_length = len;
		return (CRYPTO_BUFFER_TOO_SMALL);
	}

	return (CRYPTO_SUCCESS);
}

int
crypto_put_output_data(uchar_t *buf, crypto_data_t *output, int len)
{
	switch (output->cd_format) {
	case CRYPTO_DATA_RAW:
		if (output->cd_raw.iov_len < len) {
			output->cd_length = len;
			return (CRYPTO_BUFFER_TOO_SMALL);
		}
		memcpy((uchar_t *)(output->cd_raw.iov_base +
		    output->cd_offset), buf, len);
		break;

	case CRYPTO_DATA_UIO:
		return (crypto_uio_copy_to_data(output, buf, len));
	default:
		return (CRYPTO_ARGUMENTS_BAD);
	}

	return (CRYPTO_SUCCESS);
}

int
crypto_update_iov(void *ctx, crypto_data_t *input, crypto_data_t *output,
    int (*cipher)(void *, caddr_t, size_t, crypto_data_t *))
{
	ASSERT(input != output);

	if (input->cd_raw.iov_len < input->cd_length)
		return (CRYPTO_ARGUMENTS_BAD);

	return ((cipher)(ctx, input->cd_raw.iov_base + input->cd_offset,
	    input->cd_length, output));
}

int
crypto_update_uio(void *ctx, crypto_data_t *input, crypto_data_t *output,
    int (*cipher)(void *, caddr_t, size_t, crypto_data_t *))
{
	zfs_uio_t *uiop = input->cd_uio;
	off_t offset = input->cd_offset;
	size_t length = input->cd_length;
	uint_t vec_idx;
	size_t cur_len;

	ASSERT(input != output);

	if (zfs_uio_segflg(input->cd_uio) != UIO_SYSSPACE) {
		return (CRYPTO_ARGUMENTS_BAD);
	}

	/*
	 * Jump to the first iovec containing data to be
	 * processed.
	 */
	offset = zfs_uio_index_at_offset(uiop, offset, &vec_idx);
	if (vec_idx == zfs_uio_iovcnt(uiop) && length > 0) {
		/*
		 * The caller specified an offset that is larger than the
		 * total size of the buffers it provided.
		 */
		return (CRYPTO_DATA_LEN_RANGE);
	}

	/*
	 * Now process the iovecs.
	 */
	while (vec_idx < zfs_uio_iovcnt(uiop) && length > 0) {
		cur_len = MIN(zfs_uio_iovlen(uiop, vec_idx) -
		    offset, length);

		int rv = (cipher)(ctx, zfs_uio_iovbase(uiop, vec_idx) + offset,
		    cur_len, output);

		if (rv != CRYPTO_SUCCESS) {
			return (rv);
		}
		length -= cur_len;
		vec_idx++;
		offset = 0;
	}

	if (vec_idx == zfs_uio_iovcnt(uiop) && length > 0) {
		/*
		 * The end of the specified iovec's was reached but
		 * the length requested could not be processed, i.e.
		 * The caller requested to digest more data than it provided.
		 */

		return (CRYPTO_DATA_LEN_RANGE);
	}

	return (CRYPTO_SUCCESS);
}
