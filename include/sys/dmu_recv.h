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
