/*
 * CDDL HEADER START
 *
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 *
 * CDDL HEADER END
 */

/*
 * Copyright (c) 2020 by Delphix. All rights reserved.
 */

#ifndef	_ZSTREAM_H
#define	_ZSTREAM_H

#ifdef	__cplusplus
extern "C" {
#endif

extern void *safe_calloc(size_t n);
extern int sfread(void *buf, size_t size, FILE *fp);
extern void *safe_malloc(size_t size);
extern int zstream_do_redup(int, char *[]);
extern int zstream_do_dump(int, char *[]);
extern int zstream_do_decompress(int argc, char *argv[]);
extern int zstream_do_recompress(int argc, char *argv[]);
extern int zstream_do_token(int, char *[]);
extern void zstream_usage(void);

#ifdef	__cplusplus
}
#endif

#endif	/* _ZSTREAM_H */
