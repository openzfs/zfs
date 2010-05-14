/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
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

#ifndef	_SYS_FM_UTIL_H
#define	_SYS_FM_UTIL_H



#ifdef	__cplusplus
extern "C" {
#endif

#include <sys/nvpair.h>

/*
 * Shared user/kernel definitions for class length, error channel name,
 * and kernel event publisher string.
 */
#define	FM_MAX_CLASS 100
#define	FM_ERROR_CHAN	"com.sun:fm:error"
#define	FM_PUB		"fm"

/*
 * ereport dump device transport support
 *
 * Ereports are written out to the dump device at a proscribed offset from the
 * end, similar to in-transit log messages.  The ereports are represented as a
 * erpt_dump_t header followed by ed_size bytes of packed native nvlist data.
 *
 * NOTE: All of these constants and the header must be defined so they have the
 * same representation for *both* 32-bit and 64-bit producers and consumers.
 */
#define	ERPT_MAGIC	0xf00d4eddU
#define	ERPT_MAX_ERRS	16
#define	ERPT_DATA_SZ	(6 * 1024)
#define	ERPT_EVCH_MAX	256
#define	ERPT_HIWAT	64

typedef struct erpt_dump {
	uint32_t ed_magic;	/* ERPT_MAGIC or zero to indicate end */
	uint32_t ed_chksum;	/* checksum32() of packed nvlist data */
	uint32_t ed_size;	/* ereport (nvl) fixed buf size */
	uint32_t ed_pad;	/* reserved for future use */
	hrtime_t ed_hrt_nsec;	/* hrtime of this ereport */
	hrtime_t ed_hrt_base;	/* hrtime sample corresponding to ed_tod_base */
	struct {
		uint64_t sec;	/* seconds since gettimeofday() Epoch */
		uint64_t nsec;	/* nanoseconds past ed_tod_base.sec */
	} ed_tod_base;
} erpt_dump_t;

#ifdef _KERNEL

#include <sys/systm.h>
#include <sys/zfs_ioctl.h>

#define ZEVENT_SHUTDOWN	0x1

typedef void zevent_cb_t(nvlist_t *);

typedef struct zevent_s {
	nvlist_t	*ev_nvl;     /* protected by the zevent_lock */
	list_t		ev_zpd_list; /* " */
	list_node_t	ev_node;     /* " */
	zevent_cb_t	*ev_cb;      /* " */
} zevent_t;

typedef struct zfs_private_data {
	zevent_t	*zpd_zevent; /* protected by the zevent_lock */
	list_node_t	zpd_node;    /* " */
	uint64_t	zpd_dropped; /* " */
} zfs_private_data_t;

extern void fm_init(void);
extern void fm_fini(void);
extern void fm_nvprint(nvlist_t *);
extern void fm_zevent_init(zfs_private_data_t *);
extern void fm_zevent_fini(zfs_private_data_t *);
extern void fm_zevent_post(nvlist_t *, zevent_cb_t *);
extern void fm_zevent_drain_all(int *);
extern int fm_zevent_next(zfs_private_data_t *, zfs_cmd_t *);
extern int fm_zevent_wait(zfs_private_data_t *);

#else

static inline void fm_init(void) { }
static inline void fm_fini(void) { }

#endif  /* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_FM_UTIL_H */
