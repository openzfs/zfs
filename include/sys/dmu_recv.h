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
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2012, 2020 by Delphix. All rights reserved.
 * Copyright 2011 Nexenta Systems, Inc. All rights reserved.
 * Copyright (c) 2013, Joyent, Inc. All rights reserved.
 * Copyright (c) 2019 Datto Inc.
 */

#ifndef _DMU_RECV_H
#define	_DMU_RECV_H

#include <sys/inttypes.h>
#include <sys/types.h>
#include <sys/dsl_bookmark.h>
#include <sys/dsl_dataset.h>
#include <sys/spa.h>
#include <sys/objlist.h>
#include <sys/zfs_ioctl.h>

extern const char *const recv_clone_name;

typedef struct dmu_recv_cookie {
	struct dsl_dataset *drc_ds;
	struct dmu_replay_record *drc_drr_begin;
	struct drr_begin *drc_drrb;
	const char *drc_tofs;
	const char *drc_tosnap;
	boolean_t drc_newfs;
	boolean_t drc_byteswap;
	uint64_t drc_featureflags;
	boolean_t drc_force;
	boolean_t drc_heal;
	boolean_t drc_resumable;
	boolean_t drc_should_save;
	boolean_t drc_raw;
	boolean_t drc_clone;
	boolean_t drc_spill;
	/* This non-raw incremental diverges the ivset of a raw lineage. */
	boolean_t drc_ivset_diverged;
	nvlist_t *drc_keynvl;
	uint64_t drc_fromsnapobj;
	uint64_t drc_ivset_guid;
	void *drc_owner;
	cred_t *drc_cred;
	nvlist_t *drc_begin_nvl;
	nvlist_t *drc_errors;

	objset_t *drc_os;
	zfs_file_t *drc_fp; /* The file to read the stream from */
	uint64_t drc_voff; /* The current offset in the stream */
	uint64_t drc_bytes_read;
	/*
	 * A record that has had its payload read in, but hasn't yet been handed
	 * off to the worker thread.
	 */
	struct receive_record_arg *drc_rrd;
	/* A record that has had its header read in, but not its payload. */
	struct receive_record_arg *drc_next_rrd;
	zio_cksum_t drc_cksum;
	zio_cksum_t drc_prev_cksum;
	/* Sorted list of objects not to issue prefetches for. */
	objlist_t *drc_ignore_objlist;
} dmu_recv_cookie_t;

int dmu_recv_begin(const char *, const char *, dmu_replay_record_t *,
    boolean_t, boolean_t, boolean_t, nvlist_t *, nvlist_t *, const char *,
    dmu_recv_cookie_t *, zfs_file_t *, offset_t *);
int dmu_recv_stream(dmu_recv_cookie_t *, offset_t *);
int dmu_recv_end(dmu_recv_cookie_t *, void *);
boolean_t dmu_objset_is_receiving(objset_t *);

/*
 * Receive stream record validators.  spa may be NULL to validate against the
 * largest supported pool limits (for userland tools such as zstream).  errbuf
 * is optional; when provided it receives a short description on failure.
 *
 * Size fields that exceed a pool or on-wire maximum return ERANGE; other
 * malformed or inconsistent records return EINVAL.  Callers of lzc_receive*
 * may therefore observe ERANGE where older OpenZFS modules returned EINVAL
 * for the same oversized record.
 */
#define	RECV_CHECK_ERRBUFLEN	256

int recv_check_drr_object(const struct drr_object *, spa_t *, boolean_t raw,
    boolean_t spill, uint64_t featureflags, char *errbuf, size_t errbuflen);
int recv_check_drr_free(const struct drr_free *, char *errbuf,
    size_t errbuflen);
int recv_check_drr_freeobjects(const struct drr_freeobjects *, char *errbuf,
    size_t errbuflen);
int recv_check_drr_object_range(const struct drr_object_range *, boolean_t raw,
    char *errbuf, size_t errbuflen);
int recv_check_drr_spill(const struct drr_spill *, spa_t *, boolean_t raw,
    uint64_t featureflags, char *errbuf, size_t errbuflen);
int recv_check_drr_write(const struct drr_write *, spa_t *, boolean_t raw,
    uint64_t featureflags, char *errbuf, size_t errbuflen);
int recv_check_drr_write_embedded(const struct drr_write_embedded *, spa_t *,
    boolean_t raw, uint64_t featureflags, char *errbuf, size_t errbuflen);

#endif /* _DMU_RECV_H */
