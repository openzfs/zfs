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
 * Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef	_SYS_KSTAT_H
#define	_SYS_KSTAT_H

#include <sys/types.h>
#include <sys/time.h>

#define	KSTAT_STRLEN	255	/* 254 chars + NULL; must be 16 * n - 1 */

typedef struct kstat {
	uchar_t		ks_flags;
	void		*ks_data;
	uint_t		ks_ndata;
	size_t		ks_data_size;
	int		(*ks_update)(struct kstat *, int);
	void		*ks_private;
	void		*ks_lock;
} kstat_t;

#define	KSTAT_TYPE_RAW		0
#define	KSTAT_TYPE_NAMED	1

#define	KSTAT_FLAG_VIRTUAL		0x01
#define	KSTAT_FLAG_NO_HEADERS		0x80

#define	KSTAT_READ	0
#define	KSTAT_WRITE	1

typedef struct kstat_named {
	char	name[KSTAT_STRLEN];
	uchar_t	data_type;
	union {
		struct {
			union {
				char 		*ptr;
				char 		__pad[8];
			} addr;
			uint32_t	len;
		} str;
		int64_t		i64;
		uint64_t	ui64;
	} value;
} kstat_named_t;

#define	KSTAT_DATA_UINT32	2
#define	KSTAT_DATA_INT64	3
#define	KSTAT_DATA_UINT64	4
#define	KSTAT_DATA_STRING	9

#define	KSTAT_NAMED_PTR(kptr)		((kstat_named_t *)(kptr)->ks_data)
#define	KSTAT_NAMED_STR_PTR(knptr)	((knptr)->value.str.addr.ptr)
#define	KSTAT_NAMED_STR_BUFLEN(knptr)	((knptr)->value.str.len)

/*
 * kstat creation, installation and deletion
 */
extern kstat_t *kstat_create(const char *, int,
    const char *, const char *, uchar_t, ulong_t, uchar_t);
extern void kstat_install(kstat_t *);
extern void kstat_delete(kstat_t *);
extern void kstat_set_raw_ops(kstat_t *ksp,
    int (*headers)(char *buf, size_t size),
    int (*data)(char *buf, size_t size, void *data),
    void *(*addr)(kstat_t *ksp, off_t index));

#endif	/* _SYS_KSTAT_H */
