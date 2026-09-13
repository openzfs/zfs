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
 * Copyright (c) 2026 by Garth Snyder. All rights reserved.
 */

#ifndef	_ZSTREAM_UTIL_H
#define	_ZSTREAM_UTIL_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <assert.h>
#include <libzfs.h>
#include <pthread.h>
#include <signal.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stdtypes.h>
#include <sys/spa_checksum.h>
#include <sys/zfs_ioctl.h>
#include <sys/zio_checksum.h>
#include <sys/zio_compress.h>

/*
 * As with the libzfs-native ZIO_* encodings, only zstd compression has a
 * separately-defined level. gzip levels are bundled into the compression
 * type.
 */
typedef struct {
	enum zio_compress	cs_type;
	int			cs_level;
} compression_spec_t;

typedef struct {
	uint64_t		rs_object;
	uint64_t		rs_offset;
	compression_spec_t	rs_compression;
} record_specifier_t;

typedef void *
thread_f(void *);

extern libzfs_handle_t *libzfs_handle;

/*
 * The safe_ versions of the functions below terminate the process if the
 * operation doesn't succeed instead of returning an error.
 */
void *
safe_malloc(size_t size);

void *
safe_calloc(size_t n);

void
safe_pthread_sigmask(int how, const sigset_t *set, sigset_t *oldset);

pthread_t
safe_create_thread(thread_f *body, void *body_arg, const char *name,
    boolean_t detach);

char *
checksum_str(zio_cksum_t *cksum, char *buff, size_t buff_size);

/*
 * Prints an error message if checksums don't match. Returns B_TRUE for
 * a match, B_FALSE otherwise.
 */
boolean_t
validate_checksum(zio_cksum_t *expect, zio_cksum_t *actual, boolean_t swap,
    const char *where, off_t stream_offset);

static inline void
validate_or_exit(zio_cksum_t *expect, zio_cksum_t *actual, boolean_t swap,
    const char *where, off_t stream_offset)
{
	if (!validate_checksum(expect, actual, swap, where, stream_offset)) {
		exit(1);
	}
}

/*
 * Determine whether a compression type indicates no compression
 */
static inline boolean_t
ctype_is_uncompressed(enum zio_compress ct)
{
	VERIFY3U((int)ct, <, (int)ZIO_COMPRESS_FUNCTIONS);
	return (zio_compress_table[(int)(ct)].ci_compress == NULL);
}

/*
 * Initialize and deinitialize libzfs idempotently. libzfs has some external
 * dependencies, so it should not be initialized as a matter of course.
 */
void
require_libzfs(void);

void
release_libzfs(void);

/*
 * NOTE: This function calls require_libzfs(). Call release_libzfs() at some
 * point after use.
 *
 * Converts a string such as "zstd-12" to a compression_spec_t. Returns 0
 * for successful parsing, nonzero if parsing failed. In the case of
 * failure, the original compression_spec_t remains unmodified. This parser
 * rejects compression type "on" as being insufficiently specific.
 */
int
parse_compression_specifier(const char *str, compression_spec_t *spec);

/*
 * Reads as many OBJECT,OFFSET[,COMPRESSION] record specifiers from the
 * command line as possible, entering them into an hcreate() hash table. The
 * OBJECT/OFFSET pairs become the keys and the compression types become the
 * values. If accept_compression is B_FALSE, ZIO_COMPRESS_INHERIT is used as
 * a placeholder value. This is also the default when accept_compression is
 * B_TRUE but no compression is specified.
 *
 * Stops at the first unparseable specifier and returns the number of
 * specifiers successfully parsed. Checks a few return codes that should
 * never fail and exits with a message if they do.
 */
int
parse_record_specifiers(int argc, char *argv[], boolean_t accept_compression);

/*
 * Looks up record specifiers in an hcreate() hash table by object and
 * offset. Returns B_TRUE and sets the ctype if found.
 */
boolean_t
lookup_record_specifier(uint64_t object, uint64_t offset,
    enum zio_compress *ctype);

/*
 * Frees the hash table used by lookup_record_specifier().
 */
void
destroy_record_specifier_hash(void);

boolean_t
write_is_encrypted(struct drr_write *drrw);

uint8_t *
decompress_buffer(uint8_t *inbuff, size_t inbuff_size, size_t logical_size,
	enum zio_compress compress_type);

uint8_t *
compress_buffer(uint8_t *inbuff, size_t inbuff_size,
    compression_spec_t compress_type, size_t *compressed_size);

#ifdef __cplusplus
}
#endif

#endif  /* _ZSTREAM_UTIL_H */
