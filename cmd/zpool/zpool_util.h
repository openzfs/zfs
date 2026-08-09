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
 */

#ifndef	ZPOOL_UTIL_H
#define	ZPOOL_UTIL_H

#include <libnvpair.h>
#include <libzfs.h>
#include <libzutil.h>

#ifdef	__cplusplus
extern "C" {
#endif

/* Path to scripts you can run with "zpool status/iostat -c" */
#define	ZPOOL_SCRIPTS_DIR SYSCONFDIR"/zfs/zpool.d"

/*
 * Basic utility functions
 */
void *safe_malloc(size_t);
void *safe_realloc(void *, size_t);
void zpool_no_memory(void);
uint_t num_logs(nvlist_t *nv);
uint64_t array64_max(uint64_t array[], unsigned int len);

/*
 * Misc utility functions
 */
char *zpool_get_cmd_search_path(void);

/*
 * Virtual device functions
 */

nvlist_t *make_root_vdev(zpool_handle_t *zhp, nvlist_t *props, int force,
    int check_rep, boolean_t replacing, boolean_t dryrun, int argc,
    char **argv);
nvlist_t *split_mirror_vdev(zpool_handle_t *zhp, char *newname,
    nvlist_t *props, splitflags_t flags, int argc, char **argv);

/*
 * Pool list functions
 */
int for_each_pool(int, char **, boolean_t unavail, zprop_list_t **, zfs_type_t,
    boolean_t, zpool_iter_f, void *);

/* Vdev list functions */
int for_each_vdev(zpool_handle_t *zhp, pool_vdev_iter_f func, void *data);

typedef struct zpool_list zpool_list_t;

zpool_list_t *pool_list_get(int, char **, zprop_list_t **, zfs_type_t,
    boolean_t, int *);
int pool_list_refresh(zpool_list_t *);
int pool_list_iter(zpool_list_t *, int unavail, zpool_iter_f, void *);
void pool_list_free(zpool_list_t *);
int pool_list_count(zpool_list_t *);

extern libzfs_handle_t *g_zfs;


typedef	struct vdev_cmd_data
{
	char **lines;	/* Array of lines of output, minus the column name */
	int lines_cnt;	/* Number of lines in the array */

	char **cols;	/* Array of column names */
	int cols_cnt;	/* Number of column names */


	char *path;	/* vdev path */
	char *upath;	/* vdev underlying path */
	char *pool;	/* Pool name */
	char *cmd;	/* backpointer to cmd */
	char *vdev_enc_sysfs_path;	/* enclosure sysfs path (if any) */
} vdev_cmd_data_t;

typedef struct vdev_cmd_data_list
{
	char *cmd;		/* Command to run */
	unsigned int count;	/* Number of vdev_cmd_data items (vdevs) */

	/* fields used to select only certain vdevs, if requested */
	libzfs_handle_t *g_zfs;
	char **vdev_names;
	int vdev_names_count;
	int cb_name_flags;

	vdev_cmd_data_t *data;	/* Array of vdevs */

	/* List of unique column names and widths */
	char **uniq_cols;
	int uniq_cols_cnt;
	int *uniq_cols_width;

} vdev_cmd_data_list_t;

vdev_cmd_data_list_t *all_pools_for_each_vdev_run(int argc, char **argv,
    char *cmd, libzfs_handle_t *g_zfs, char **vdev_names, int vdev_names_count,
    int cb_name_flags);

void free_vdev_cmd_data_list(vdev_cmd_data_list_t *vcdl);

void free_vdev_cmd_data(vdev_cmd_data_t *data);

int vdev_run_cmd_simple(char *path, char *cmd);

int check_device(const char *path, boolean_t force,
    boolean_t isspare, boolean_t iswholedisk);
boolean_t check_sector_size_database(char *path, int *sector_size);
void vdev_error(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
int check_file(const char *file, boolean_t force, boolean_t isspare);
void after_zpool_upgrade(zpool_handle_t *zhp);
int check_file_generic(const char *file, boolean_t force, boolean_t isspare);

int zpool_power(zpool_handle_t *zhp, char *vdev, boolean_t turn_on);
int zpool_power_current_state(zpool_handle_t *zhp, char *vdev);

#ifdef	__cplusplus
}
#endif

#endif	/* ZPOOL_UTIL_H */
