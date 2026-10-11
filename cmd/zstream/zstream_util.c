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
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2011, 2020 by Delphix. All rights reserved.
 * Copyright (c) 2012, Joyent, Inc. All rights reserved.
 * Copyright (c) 2012 Pawel Jakub Dawidek <pawel@dawidek.net>.
 * All rights reserved
 * Copyright (c) 2013 Steven Hartland. All rights reserved.
 * Copyright 2015, OmniTI Computer Consulting, Inc. All rights reserved.
 * Copyright 2016 Igor Kozhukhov <ikozhukhov@gmail.com>
 * Copyright (c) 2018, loli10K <ezomori.nozomu@gmail.com>. All rights reserved.
 * Copyright (c) 2019 Datto Inc.
 * Copyright (c) 2024, Klara, Inc.
 */

#include <assert.h>
#include <err.h>
#include <errno.h>
#include <libzfs.h>
#include <libzutil.h>
#include <pthread.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/abd.h>
#include <sys/fs/zfs.h>
#include <sys/spa_checksum.h>
#include <sys/stdtypes.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <sys/zfs_ioctl.h>
#include <sys/zio.h>
#include <sys/zio_compress.h>
#include <unistd.h>

#include "zstream_util.h"
#include "zstream_hash.h"

libzfs_handle_t		*libzfs_handle = NULL;
static linear_hash_t	*record_specifiers = NULL;

void *
safe_malloc(size_t size)
{
	void *rv = malloc(size);
	if (rv == NULL) {
		errx(1, "failed to allocate %zu bytes, aborting...", size);
	}
	return (rv);
}

void *
safe_calloc(size_t size)
{
	void *rv = calloc(1, size);
	if (rv == NULL) {
		errx(1, "failed to allocate %zu bytes, aborting...", size);
	}
	return (rv);
}

void *
safe_realloc(void *buff, size_t new_size)
{
	if ((buff = realloc(buff, new_size)) == NULL && new_size != 0)
		errx(1, "failed to realloc %llu bytes, aborting...",
		    (u_longlong_t)new_size);
	return (buff);
}

void
safe_pwrite(int fd, const void *buf, size_t count, off_t offset,
    const char *where)
{
	size_t done = 0;

	while (done < count) {
		ssize_t n = pwrite(fd, (uint8_t *)buf + done, count - done,
		    offset + done);
		if (n < 0) {
			if (errno == EINTR)
				continue; /* zero progress this call, retry */
			else if ((errno == ENOSPC || errno == EDQUOT) &&
			    where != NULL)
				errx(1, "write failed, out of space in %s"
				    ", aborting...", where);
			err(1, "pwrite() failed");
		}
		if (n == 0)
			errx(1, "pwrite of %llu bytes failed",
			    (u_longlong_t)count);
		done += (size_t)n;
	}
}

void
safe_pread_zero(int fd, void *buf, size_t count, off_t offset)
{
	size_t done = 0;

	while (done < count) {
		ssize_t n = pread(fd, (uint8_t *)buf + done, count - done,
		    offset + done);
		if (n < 0) {
			if (errno == EINTR)
				continue; /* zero progress this call, retry */
			err(1, "pread failed");
		}
		if (n == 0) {
			memset((uint8_t *)buf + done, 0, count - done);
			return;
		}
		done += (size_t)n;
	}
}

void
safe_pthread_sigmask(int how, const sigset_t *set, sigset_t *oldset)
{
	int error = pthread_sigmask(how, set, oldset);
	if (error != 0) {
		errno = error;
		err(1, "pthread_sigmask failed");
	}
}

pthread_t
safe_create_thread(thread_f *body, void *body_arg, const char *name,
    boolean_t detach)
{
	pthread_t tid;
	int ret;
	int name_attempts = 3;

	ret = pthread_create(&tid, NULL, body, body_arg);
	if (ret != 0) {
		errno = ret;
		err(1, "pthread_create for %s failed", name);
	}
	/*
	 * pthread_setname_np() fails randomly on some Debian systems
	 * because of difficulty reading /proc/self/task. The characteristic
	 * error is "No such file or directory." The name is just a
	 * debugging aid, so we can ignore the error, but we'll make three
	 * attempts to set it. This code previously printed a warning
	 * message, but that interferes with zstream dump output comparisons
	 * in ZTS.
	 */
	while (name_attempts-- > 0) {
		ret = pthread_setname_np(tid, name);
		if (ret == 0)
			break;
		usleep(100);
	}
	if (detach) {
		ret = pthread_detach(tid);
		if (ret != 0) {
			errno = ret;
			err(1, "failed to detach %s thread", name);
		}
	}
	return (tid);
}

char *
checksum_str(zio_cksum_t *cksum, char *buff, size_t buff_size)
{
	snprintf(buff, buff_size, "%.16llx / %.16llx / %.16llx / %.16llx",
	    (long long unsigned int) cksum->zc_word[0],
	    (long long unsigned int) cksum->zc_word[1],
	    (long long unsigned int) cksum->zc_word[2],
	    (long long unsigned int) cksum->zc_word[3]);
	return (buff);
}

boolean_t
validate_checksum(zio_cksum_t *expected, zio_cksum_t *actual,
    boolean_t swap, const char *where, off_t stream_offset)
{
	static char buff[128];
	zio_cksum_t swapped_actual;

	if (swap) {
		swapped_actual = *actual;
		actual = &swapped_actual;
		ZIO_CHECKSUM_BSWAP(actual);
	}
	/* cppcheck-suppress uninitvar */
	if (ZIO_CHECKSUM_EQUAL(*expected, *actual)) {
		return (B_TRUE);
	}
	fflush(stdout);
	fprintf(stderr, "Incorrect checksum %s (stream offset %lld)\n", where,
	    (longlong_t)stream_offset);
	fprintf(stderr, "Expected = %s\n", checksum_str(expected, buff,
	    sizeof (buff)));
	fprintf(stderr, "  Actual = %s\n", checksum_str(actual, buff,
	    sizeof (buff)));
	return (B_FALSE);
}

/*
 * Initialize and deinitialize libzfs idempotently. libzfs has some external
 * dependencies, so it shouldn't be initialized as a matter of course.
 */
void
require_libzfs(void)
{
	if (libzfs_handle == NULL) {
		if ((libzfs_handle = libzfs_init()) == NULL) {
			errx(1, "%s", libzfs_error_init(errno));
		}
	}
}

void
release_libzfs(void)
{
	if (libzfs_handle != NULL) {
		libzfs_fini(libzfs_handle);
		libzfs_handle = NULL;
	}
}

/*
 * This function calls require_libzfs(). release_libzfs() should be called
 * at some point after parsing.
 */
int
parse_compression_specifier(const char *str, compression_spec_t *spec)
{
	uint64_t val;
	int rc;

	require_libzfs();
	rc = zfs_prop_string_to_index(ZFS_PROP_COMPRESSION, str, &val);
	if (rc == 0) {
		*spec = (compression_spec_t) {
			.cs_type = ZIO_COMPRESS_ALGO(val),
			.cs_level = ZIO_COMPRESS_LEVEL(val)
		};
		if (spec->cs_type == ZIO_COMPRESS_ON)
			errx(1, "the meaning of compression type 'on' varies "
			    "among systems - use a more specific type "
			    "such as 'lz4'");
		boolean_t is_zstd = spec->cs_type == ZIO_COMPRESS_ZSTD;
		boolean_t inherited = spec->cs_level == ZIO_COMPLEVEL_INHERIT;
		if (is_zstd && inherited)
			spec->cs_level = ZIO_COMPLEVEL_DEFAULT;
	}
	return (rc);
}

/*
 * Parse "nice" sizes such as "100M"
 */
int
parse_size_specifier(const char *str, size_t *size)
{
	uint64_t val;

	/*
	 * zfs_nicestrtonum() only uses its libzfs handle to record error
	 * details, and it accepts NULL in that position.
	 */
	if (zfs_nicestrtonum(NULL, str, &val) != 0 || (size_t)val != val)
		return (-1);
	*size = (size_t)val;
	return (0);
}

/*
 * Parse a string of the form "OBJECT,OFFSET" or "OBJECT,OFFSET,COMPRESSION"
 * (with accept_compression == B_TRUE) and fill out a corresponding
 * record_compression_t. Returns 0 if the string was successfully parsed.
 *
 * The compression type field of the record specifier struct is always
 * initialized to ZIO_COMPRESS_INHERIT, whether accept_compression is true
 * or not. If accept_compression is B_TRUE but the input string does not
 * specify compression, the value remains at ZIO_COMPRESS_INHERIT.
 *
 * If accept_compression is B_TRUE, this function (indirectly) calls
 * require_libzfs(). release_libzfs() should be called after parsing.
 */
static int
parse_record_specifier(const char *str, record_compression_t *rec,
    boolean_t accept_compression)
{
	char static_buff[256];
	char *buff = static_buff;
	char *loc;
	char *obj_str, *offset_str, *end;
	int return_code = -1;
	size_t in_size = strlen(str) + 1;

	if (in_size > sizeof (static_buff)) {
		buff = safe_malloc(in_size);
	}
	strcpy(buff, str);
	loc = buff;
	errno = 0;
	rec->rc_compression = (compression_spec_t) {
		.cs_type = ZIO_COMPRESS_INHERIT
	};

	obj_str = strsep(&loc, ",");
	if (loc == NULL)
		goto bail;
	rec->rc_key.rs_object = strtoull(obj_str, &end, 0);
	if (errno != 0 || *end != '\0' || obj_str == end)
		goto bail;
	offset_str = strsep(&loc, ",");
	rec->rc_key.rs_offset = strtoull(offset_str, &end, 0);
	if (errno != 0 || *end != '\0' || offset_str == end)
		goto bail;
	if (loc != NULL) {
		if (accept_compression) {
			int rc = parse_compression_specifier(loc,
			    &rec->rc_compression);
			if (rc != 0)
				goto bail;
		} else {
			goto bail;
		}
	}
	return_code = 0;

bail:	if (buff != static_buff)
		free(buff);
	return (return_code);
}

/*
 * Reads as many OBJECT,OFFSET[,COMPRESSION] record specifiers from the
 * command line as possible and enters them into a hash table. If
 * accept_compression is B_FALSE, ZIO_COMPRESS_INHERIT is used as a
 * placeholder value. This is also the default when accept_compression is
 * B_TRUE but no compression is specified.
 *
 * Stops at the first unparseable specifier and returns the number of
 * specifiers successfully parsed.
 */
int
parse_record_specifiers(int argc, char *argv[], boolean_t accept_compression)
{
	int num_parsed = 0;
	ASSERT(record_specifiers == NULL);
	/*
	 * A budget of 1MB or less gets page-sized clusters, so a 512KB base
	 * leaves each of the table's three allocators many clusters, however
	 * few specifiers there are. Then allow 128 bytes per specifier, about
	 * 2.5 times the measured cost of an entry.
	 */
	record_specifiers = lh_init(sizeof (record_compression_t),
	    sizeof (record_specifier_t), (1 << 19) + (size_t)argc * 128, NULL);
	for (int i = 0; i < argc; i++) {
		record_compression_t comp;
		if (parse_record_specifier(argv[i], &comp, accept_compression))
			break;
		if (lh_upsert(record_specifiers, &comp) == LH_UPDATED)
			warnx("duplicate record specifier %llu,%llu, "
			    "overwriting...",
			    (u_longlong_t)comp.rc_key.rs_object,
			    (u_longlong_t)comp.rc_key.rs_offset);
		num_parsed++;
	}
	/*
	 * require_libzfs() will not have been called if accept_compression
	 * is B_FALSE, but in that case release_libzfs() is a no-op.
	 */
	release_libzfs();
	return (num_parsed);
}

/*
 * Retrieves a raw zio_compress value rather than a compression_spec_t
 * because no clients are interested in compression levels. Sets the ctype
 * and returns B_TRUE if a matching record was found.
 */
boolean_t
lookup_record_specifier(uint64_t object, uint64_t offset,
    enum zio_compress *ctype)
{
	record_compression_t comp = { .rc_key = { object, offset } };
	if (lh_find(record_specifiers, &comp) == LH_NOT_FOUND)
		return (B_FALSE);
	*ctype = comp.rc_compression.cs_type;
	return (B_TRUE);
}

void
record_specifier_hash_fini(void)
{
	if (record_specifiers != NULL) {
		lh_fini(record_specifiers);
		record_specifiers = NULL;
	}
}

boolean_t
write_is_encrypted(struct drr_write *drrw)
{
	for (int i = 0; i < ZIO_DATA_SALT_LEN; i++) {
		if (drrw->drr_salt[i] != 0) {
			return (B_TRUE);
		}
	}
	return (B_FALSE);
}

/*
 * The specified compress_type must reflect the buffer's actual compression.
 * Returns an allocated buffer if decompression was successful, NULL
 * otherwise.
 */
uint8_t *
decompress_buffer(uint8_t *inbuff, size_t inbuff_size, size_t logical_size,
    enum zio_compress compress_type)
{
	uint8_t *outbuff = safe_malloc(logical_size);
	abd_t sabd, dabd;
	int ret;

	VERIFY3B(ctype_is_uncompressed(compress_type), ==, B_FALSE);

	abd_get_from_buf_struct(&sabd, inbuff, inbuff_size);
	abd_get_from_buf_struct(&dabd, outbuff, logical_size);
	ret = zio_decompress_data(compress_type, &sabd, &dabd,
	    inbuff_size, abd_get_size(&dabd), NULL);

	abd_free(&dabd);
	abd_free(&sabd);

	if (ret != 0) {
		free(outbuff);
		return (NULL);
	}

	return (outbuff);
}

/*
 * Returns an allocated buffer if compression was successful, NULL
 * otherwise.
 */
uint8_t *
compress_buffer(uint8_t *inbuff, size_t inbuff_size,
    compression_spec_t compress_type, size_t *compressed_size)
{
	uint8_t *outbuff = safe_malloc(inbuff_size);
	abd_t	sabd, dabd;
	size_t	csize, rounded;

	VERIFY3B(ctype_is_uncompressed(compress_type.cs_type), ==, B_FALSE);

	abd_t *pabd = abd_get_from_buf_struct(&dabd, outbuff, inbuff_size);
	abd_get_from_buf_struct(&sabd, inbuff, inbuff_size);
	csize = zio_compress_data(compress_type.cs_type, &sabd,
	    &pabd, inbuff_size, inbuff_size, compress_type.cs_level);

	rounded = P2ROUNDUP(csize, SPA_MINBLOCKSIZE);
	if (rounded < inbuff_size) {
		abd_zero_off(pabd, csize, rounded - csize);
		*compressed_size = rounded;
	} else {
		free(outbuff);
		outbuff = NULL;
	}

	abd_free(&sabd);
	abd_free(&dabd);

	return (outbuff);
}

nicebuff_t
nicenums(int count, ...)
{
	VERIFY(count > 0 && count <= MAX_NICENUMS);
	nicebuff_t buff = {0};
	va_list ap;
	va_start(ap, count);
	for (int i = 0; i < count; i++) {
		uint64_t num = va_arg(ap, uint64_t);
		zfs_nicenum(num, &buff.nice[i][0], NICENUM_BUFF_SIZE);
	}
	va_end(ap);
	return (buff);
}

int
safe_create_temp_file(const char *dir_path)
{
	char buff[1024];
	int length = snprintf(buff, sizeof (buff),
	    "%s/zstream-temp-XXXXXX", dir_path);
	if (length >= sizeof (buff))
		errx(1, "filename too long in %s", __func__);
	int fd = mkstemp(buff);
	if (fd < 0)
		err(1, "unable to create temp file in %s", dir_path);
	if (unlink(buff) < 0)
		err(1, "unlink of %s failed", buff);
	return (fd);
}
