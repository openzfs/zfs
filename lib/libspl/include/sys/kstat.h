// SPDX-License-Identifier: CDDL-1.0
/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or https://opensource.org/licenses/CDDL-1.0.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
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
    void *(*addr)(kstat_t *ksp, loff_t index));

#endif	/* _SYS_KSTAT_H */
