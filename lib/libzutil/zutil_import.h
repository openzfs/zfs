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
 * Copyright 2015 Nexenta Systems, Inc. All rights reserved.
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2012, 2018 by Delphix. All rights reserved.
 * Copyright 2015 RackTop Systems.
 * Copyright (c) 2016, Intel Corporation.
 */
#ifndef _LIBZUTIL_ZUTIL_IMPORT_H_
#define	_LIBZUTIL_ZUTIL_IMPORT_H_

#define	EZFS_BADCACHE	"invalid or missing cache file"
#define	EZFS_BADPATH	"must be an absolute path"
#define	EZFS_NOMEM	"out of memory"
#define	EZFS_EACESS	"some devices require root privileges"

#define	IMPORT_ORDER_PREFERRED_1	1
#define	IMPORT_ORDER_PREFERRED_2	2
#define	IMPORT_ORDER_SCAN_OFFSET	10
#define	IMPORT_ORDER_DEFAULT		100

typedef struct libpc_handle {
	boolean_t lpc_printerr;
	boolean_t lpc_open_access_error;
	boolean_t lpc_desc_active;
	char lpc_desc[1024];
	pool_config_ops_t *lpc_ops;
	void *lpc_lib_handle;
} libpc_handle_t;


int label_paths(libpc_handle_t *hdl, nvlist_t *label, char **path,
    char **devid);
int zpool_find_import_blkid(libpc_handle_t *hdl, pthread_mutex_t *lock,
    avl_tree_t **slice_cache);

void * zutil_alloc(libpc_handle_t *hdl, size_t size);
char *zutil_strdup(libpc_handle_t *hdl, const char *str);

typedef struct rdsk_node {
	char *rn_name;			/* Full path to device */
	int rn_order;			/* Preferred order (low to high) */
	int rn_num_labels;		/* Number of valid labels */
	uint64_t rn_vdev_guid;		/* Expected vdev guid when set */
	libpc_handle_t *rn_hdl;
	nvlist_t *rn_config;		/* Label config */
	avl_tree_t *rn_avl;
	avl_node_t rn_node;
	pthread_mutex_t *rn_lock;
	boolean_t rn_labelpaths;
} rdsk_node_t;

int slice_cache_compare(const void *, const void *);

void zpool_open_func(void *);

#endif /* _LIBZUTIL_ZUTIL_IMPORT_H_ */
