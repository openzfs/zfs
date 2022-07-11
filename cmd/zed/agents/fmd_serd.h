/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
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
extern void fmd_serd_eng_gc(fmd_serd_eng_t *);

#ifdef	__cplusplus
}
#endif

#endif	/* _FMD_SERD_H */
