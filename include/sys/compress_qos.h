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


#ifndef	_SYS_COMPRESS_QOS_H
#define	_SYS_COMPRESS_QOS_H

#define	QOS_COMPESS_LEVELS 10

size_t qos_compress(zio_t *zio, enum zio_compress *c, abd_t *src, void *dst,
    size_t s_len);

#endif /* _SYS_COMPRESS_QOS_H */
