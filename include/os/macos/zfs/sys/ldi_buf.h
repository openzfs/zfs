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
 * Copyright (c) 2015, Evan Susarret.  All rights reserved.
 *
 * OS X implementation of ldi_ named functions for ZFS written by
 * Evan Susarret in 2015.
 */

#ifndef _SYS_LDI_BUF_H
#define	_SYS_LDI_BUF_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/*
 * Can not include C++ header in C, so we make space for it here.
 * We check it is enough space with CTASSERT in ldi_iokit.cpp.
 * If we one day compile everything with C++ we can embed IOStorageCompletion
 * directly here.
 */
struct opaque_iocompletion {
	void *space[3];
};

/*
 * Buffer context for LDI strategy
 */
typedef struct ldi_buf {
	/* For client use */
	int		(*b_iodone)(struct ldi_buf *); /* Callback */
	union {
		void	*b_addr;	/* Passed buffer address */
	} b_un;				/* Union to match illumos */
	uint64_t	b_bcount;	/* Size of IO */
	uint64_t	b_bufsize;	/* Size of buffer */
	uint64_t	b_lblkno;	/* logical block number */
	uint64_t	b_resid;	/* Remaining IO size */
	int		b_flags;	/* Read or write, options */
	int		b_error;	/* IO error code */
	void	*b_private; /* caller own ptr */
	struct opaque_iocompletion b_completion;
} ldi_buf_t;				/* XXX Currently 64b */

ldi_buf_t *ldi_getrbuf(int);
void ldi_freerbuf(ldi_buf_t *);
void ldi_bioinit(ldi_buf_t *);

/* Define macros to get and release a buffer */
#define	getrbuf(flags)	ldi_getrbuf(flags)
#define	freerbuf(lbp)	ldi_freerbuf(lbp)
#define	bioinit(lbp)	ldi_bioinit(lbp)
#define	geterror(lbp)	(lbp->b_error)
#define	biowait(lbp)	(0)

#define	lbtodb(bytes) \
	(bytes >> DEV_BSHIFT)
#define	dbtolb(blkno) \
	(blkno << DEV_BSHIFT)
#define	ldbtob(blkno)	dbtolb(blkno)

/* Redefine B_BUSY */
#define	B_BUSY	B_PHYS

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* _SYS_LDI_BUF_H */
