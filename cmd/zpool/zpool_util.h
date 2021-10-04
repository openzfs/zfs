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
void zpool_no_memory(void);
uint_t num_logs(nvlist_t *nv);
uint64_t array64_max(uint64_t array[], unsigned int len);
int highbit64(uint64_t i);
int lowbit64(uint64_t i);

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
int for_each_pool(int, char **, boolean_t unavail, zprop_list_t **,
    boolean_t, zpool_iter_f, void *);

/* Vdev list functions */
int for_each_vdev(zpool_handle_t *zhp, pool_vdev_iter_f func, void *data);

typedef struct zpool_list zpool_list_t;

zpool_list_t *pool_list_get(int, char **, zprop_list_t **, boolean_t, int *);
void pool_list_update(zpool_list_t *);
int pool_list_iter(zpool_list_t *, int unavail, zpool_iter_f, void *);
void pool_list_free(zpool_list_t *);
int pool_list_count(zpool_list_t *);
void pool_list_remove(zpool_list_t *, zpool_handle_t *);

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

int check_device(const char *path, boolean_t force,
    boolean_t isspare, boolean_t iswholedisk);
boolean_t check_sector_size_database(char *path, int *sector_size);
void vdev_error(const char *fmt, ...);
int check_file(const char *file, boolean_t force, boolean_t isspare);
void after_zpool_upgrade(zpool_handle_t *zhp);

#ifdef	__cplusplus
}
#endif

#endif	/* ZPOOL_UTIL_H */
