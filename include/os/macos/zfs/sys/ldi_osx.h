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
 * Copyright (c) 1994, 2010, Oracle and/or its affiliates. All rights reserved.
 */
/*
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */
/*
 * Copyright (c) 2013, Joyent, Inc.  All rights reserved.
 */
/*
 * Copyright (c) 2015, Evan Susarret.  All rights reserved.
 */
/*
 * Portions of this document are copyright Oracle and Joyent.
 * OS X implementation of ldi_ named functions for ZFS written by
 * Evan Susarret in 2015.
 */

#ifndef _SYS_LDI_OSX_H
#define	_SYS_LDI_OSX_H

#include <sys/ldi_buf.h>

/*
 * OS X - The initialization/destructor functions are available
 * for zfs-osx.cpp to call during zfs_init/zfs_fini.
 */
#ifdef __cplusplus
extern "C" {

int ldi_init(void *);		/* passes IOService provider */
void ldi_fini();		/* teardown */
#endif /* __cplusplus */

/*
 * Opaque layered driver data structures.
 * vdev_disk and other C callers may use these LDI interfaces
 * ldi_ident_t is already defined as typedef void* by spl sunddi.h
 */
typedef struct __ldi_handle		*ldi_handle_t;
typedef struct __ldi_callback_id	*ldi_callback_id_t;
typedef struct __ldi_ev_cookie		*ldi_ev_cookie_t;

/*
 * LDI event interface related
 */
#define	LDI_EV_SUCCESS	0
#define	LDI_EV_FAILURE	(-1)
#define	LDI_EV_NONE	(-2)	/* no matching callbacks registered */
#define	LDI_EV_OFFLINE	"LDI:EVENT:OFFLINE"
#define	LDI_EV_DEGRADE	"LDI:EVENT:DEGRADE"
#define	LDI_EV_DEVICE_REMOVE	"LDI:EVENT:DEVICE_REMOVE"

#define	LDI_EV_CB_VERS_1	1
#define	LDI_EV_CB_VERS		LDI_EV_CB_VERS_1

typedef struct ldi_ev_callback {
	uint_t cb_vers;
	int (*cb_notify)(ldi_handle_t, ldi_ev_cookie_t, void *, void *);
	void (*cb_finalize)(ldi_handle_t, ldi_ev_cookie_t, int,
	    void *, void *);
} ldi_ev_callback_t;

/* Structs passed to media_get_info */
struct dk_minfo {
	uint32_t		dki_capacity;	/* Logical block count */
	uint32_t		dki_lbsize;	/* Logical block size */
};	/* (8b) */

struct dk_minfo_ext {
	uint64_t		dki_capacity;	/* Logical block count */
	uint32_t		dki_lbsize;	/* Logical block size */
	uint32_t		dki_pbsize;	/* Physical block size */
};	/* (16b) */

struct io_bootinfo {
	char			dev_path[MAXPATHLEN];	/* IODeviceTree path */
	uint64_t		dev_size;	/* IOMedia device size */
};

/*
 * XXX This struct is defined in spl but was unused until now.
 * There is a reference in zvol.c zvol_ioctl, commented out.
 */
#if 0
struct dk_callback {
	void (*dkc_callback)(void *dkc_cookie, int error);
	void *dkc_cookie;
	int dkc_flag;
};	/* XXX Currently 20b */
#endif

/* XXX Already defined in spl dkio.h (used elsewhere) */
#if 0
#define	DKIOCFLUSHWRITECACHE	(DKIOC | 34)
#endif

#define	FLUSH_VOLATILE		0x1
#define	DKIOCGMEDIAINFOEXT	(DKIOC | 48)

/* XXX Created this additional ioctl */
#define	DKIOCGETBOOTINFO	(DKIOC | 99)

/*
 * LDI Handle manipulation functions
 */
int ldi_open_by_dev(dev_t, int, int, cred_t *,
    ldi_handle_t *, __unused ldi_ident_t);
int ldi_open_by_name(char *, int, cred_t *,
    ldi_handle_t *, __unused ldi_ident_t);

int ldi_close(ldi_handle_t, int, cred_t *);

int ldi_sync(ldi_handle_t);
int ldi_get_size(ldi_handle_t, uint64_t *);
int ldi_ioctl(ldi_handle_t, int, intptr_t, int, cred_t *, int *);
int ldi_strategy(ldi_handle_t, ldi_buf_t *);

/*
 * LDI events related declarations
 */
extern int ldi_ev_get_cookie(ldi_handle_t, char *, ldi_ev_cookie_t *);
extern char *ldi_ev_get_type(ldi_ev_cookie_t);
extern int ldi_ev_register_callbacks(ldi_handle_t, ldi_ev_cookie_t,
    ldi_ev_callback_t *, void *, ldi_callback_id_t *);
extern int ldi_ev_remove_callbacks(ldi_callback_id_t);

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* _SYS_LDI_OSX_H */
