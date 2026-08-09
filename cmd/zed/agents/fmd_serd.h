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
 * Copyright 2004 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2016, Intel Corporation.
 */

#ifndef	_FMD_SERD_H
#define	_FMD_SERD_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <sys/list.h>
#include <sys/time.h>

typedef struct fmd_serd_elem {
	list_node_t	se_list;	/* linked list forward/back pointers */
	hrtime_t	se_hrt;		/* upper bound on event hrtime */
} fmd_serd_elem_t;

typedef struct fmd_serd_eng {
	char		*sg_name;	/* string name for this engine */
	struct fmd_serd_eng *sg_next;	/* next engine on hash chain */
	list_t		sg_list;	/* list of fmd_serd_elem_t's */
	uint_t		sg_count;	/* count of events in sg_list */
	uint_t		sg_flags;	/* engine flags (see below) */
	uint_t		sg_n;		/* engine N parameter (event count) */
	hrtime_t	sg_t;		/* engine T parameter (nanoseconds) */
} fmd_serd_eng_t;

#define	FMD_SERD_FIRED	0x1		/* error rate has exceeded threshold */
#define	FMD_SERD_DIRTY	0x2		/* engine needs to be checkpointed */

typedef void fmd_serd_eng_f(fmd_serd_eng_t *, void *);

typedef struct fmd_serd_hash {
	fmd_serd_eng_t	**sh_hash;	/* hash bucket array for buffers */
	uint_t		sh_hashlen;	/* length of hash bucket array */
	uint_t		sh_count;	/* count of engines in hash */
} fmd_serd_hash_t;

extern void fmd_serd_hash_create(fmd_serd_hash_t *);
extern void fmd_serd_hash_destroy(fmd_serd_hash_t *);
extern void fmd_serd_hash_apply(fmd_serd_hash_t *, fmd_serd_eng_f *, void *);

extern fmd_serd_eng_t *fmd_serd_eng_insert(fmd_serd_hash_t *,
    const char *, uint32_t, hrtime_t);

extern fmd_serd_eng_t *fmd_serd_eng_lookup(fmd_serd_hash_t *, const char *);
extern void fmd_serd_eng_delete(fmd_serd_hash_t *, const char *);

extern int fmd_serd_eng_record(fmd_serd_eng_t *, hrtime_t);
extern int fmd_serd_eng_fired(fmd_serd_eng_t *);
extern int fmd_serd_eng_empty(fmd_serd_eng_t *);

extern void fmd_serd_eng_reset(fmd_serd_eng_t *);
extern void fmd_serd_eng_gc(fmd_serd_eng_t *, void *);

#ifdef	__cplusplus
}
#endif

#endif	/* _FMD_SERD_H */
