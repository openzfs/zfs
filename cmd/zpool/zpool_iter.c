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
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * Copyright 2016 Igor Kozhukhov <ikozhukhov@gmail.com>.
 */

#include <libintl.h>
#include <libuutil.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <thread_pool.h>

#include <libzfs.h>
#include <libzutil.h>
#include <sys/zfs_context.h>
#include <sys/wait.h>

#include "zpool_util.h"

/*
 * Private interface for iterating over pools specified on the command line.
 * Most consumers will call for_each_pool, but in order to support iostat, we
 * allow fined grained control through the zpool_list_t interface.
 */

typedef struct zpool_node {
	zpool_handle_t	*zn_handle;
	uu_avl_node_t	zn_avlnode;
	int		zn_mark;
} zpool_node_t;

struct zpool_list {
	boolean_t	zl_findall;
	boolean_t	zl_literal;
	uu_avl_t	*zl_avl;
	uu_avl_pool_t	*zl_pool;
	zprop_list_t	**zl_proplist;
	zfs_type_t	zl_type;
};

static int
zpool_compare(const void *larg, const void *rarg, void *unused)
{
	(void) unused;
	zpool_handle_t *l = ((zpool_node_t *)larg)->zn_handle;
	zpool_handle_t *r = ((zpool_node_t *)rarg)->zn_handle;
	const char *lname = zpool_get_name(l);
	const char *rname = zpool_get_name(r);

	return (strcmp(lname, rname));
}

/*
 * Callback function for pool_list_get().  Adds the given pool to the AVL tree
 * of known pools.
 */
static int
add_pool(zpool_handle_t *zhp, void *data)
{
	zpool_list_t *zlp = data;
	zpool_node_t *node = safe_malloc(sizeof (zpool_node_t));
	uu_avl_index_t idx;

	node->zn_handle = zhp;
	uu_avl_node_init(node, &node->zn_avlnode, zlp->zl_pool);
	if (uu_avl_find(zlp->zl_avl, node, NULL, &idx) == NULL) {
		if (zlp->zl_proplist &&
		    zpool_expand_proplist(zhp, zlp->zl_proplist,
		    zlp->zl_type, zlp->zl_literal) != 0) {
			zpool_close(zhp);
			free(node);
			return (-1);
		}
		uu_avl_insert(zlp->zl_avl, node, idx);
	} else {
		zpool_close(zhp);
		free(node);
		return (-1);
	}

	return (0);
}

/*
 * Create a list of pools based on the given arguments.  If we're given no
 * arguments, then iterate over all pools in the system and add them to the AVL
 * tree.  Otherwise, add only those pool explicitly specified on the command
 * line.
 */
zpool_list_t *
pool_list_get(int argc, char **argv, zprop_list_t **proplist, zfs_type_t type,
    boolean_t literal, int *err)
{
	zpool_list_t *zlp;

	zlp = safe_malloc(sizeof (zpool_list_t));

	zlp->zl_pool = uu_avl_pool_create("zfs_pool", sizeof (zpool_node_t),
	    offsetof(zpool_node_t, zn_avlnode), zpool_compare, UU_DEFAULT);

	if (zlp->zl_pool == NULL)
		zpool_no_memory();

	if ((zlp->zl_avl = uu_avl_create(zlp->zl_pool, NULL,
	    UU_DEFAULT)) == NULL)
		zpool_no_memory();

	zlp->zl_proplist = proplist;
	zlp->zl_type = type;

	zlp->zl_literal = literal;

	if (argc == 0) {
		(void) zpool_iter(g_zfs, add_pool, zlp);
		zlp->zl_findall = B_TRUE;
	} else {
		int i;

		for (i = 0; i < argc; i++) {
			zpool_handle_t *zhp;

			if ((zhp = zpool_open_canfail(g_zfs, argv[i])) !=
			    NULL) {
				if (add_pool(zhp, zlp) != 0)
					*err = B_TRUE;
			} else {
				*err = B_TRUE;
			}
		}
	}

	return (zlp);
}

/*
 * Search for any new pools, adding them to the list.  We only add pools when no
 * options were given on the command line.  Otherwise, we keep the list fixed as
 * those that were explicitly specified.
 */
void
pool_list_update(zpool_list_t *zlp)
{
	if (zlp->zl_findall)
		(void) zpool_iter(g_zfs, add_pool, zlp);
}

/*
 * Iterate over all pools in the list, executing the callback for each
 */
int
pool_list_iter(zpool_list_t *zlp, int unavail, zpool_iter_f func,
    void *data)
{
	zpool_node_t *node, *next_node;
	int ret = 0;

	for (node = uu_avl_first(zlp->zl_avl); node != NULL; node = next_node) {
		next_node = uu_avl_next(zlp->zl_avl, node);
		if (zpool_get_state(node->zn_handle) != POOL_STATE_UNAVAIL ||
		    unavail)
			ret |= func(node->zn_handle, data);
	}

	return (ret);
}

/*
 * Remove the given pool from the list.  When running iostat, we want to remove
 * those pools that no longer exist.
 */
void
pool_list_remove(zpool_list_t *zlp, zpool_handle_t *zhp)
{
	zpool_node_t search, *node;

	search.zn_handle = zhp;
	if ((node = uu_avl_find(zlp->zl_avl, &search, NULL, NULL)) != NULL) {
		uu_avl_remove(zlp->zl_avl, node);
		zpool_close(node->zn_handle);
		free(node);
	}
}

/*
 * Free all the handles associated with this list.
 */
void
pool_list_free(zpool_list_t *zlp)
{
	uu_avl_walk_t *walk;
	zpool_node_t *node;

	if ((walk = uu_avl_walk_start(zlp->zl_avl, UU_WALK_ROBUST)) == NULL) {
		(void) fprintf(stderr,
		    gettext("internal error: out of memory"));
		exit(1);
	}

	while ((node = uu_avl_walk_next(walk)) != NULL) {
		uu_avl_remove(zlp->zl_avl, node);
		zpool_close(node->zn_handle);
		free(node);
	}

	uu_avl_walk_end(walk);
	uu_avl_destroy(zlp->zl_avl);
	uu_avl_pool_destroy(zlp->zl_pool);

	free(zlp);
}

/*
 * Returns the number of elements in the pool list.
 */
int
pool_list_count(zpool_list_t *zlp)
{
	return (uu_avl_numnodes(zlp->zl_avl));
}

/*
 * High level function which iterates over all pools given on the command line,
 * using the pool_list_* interfaces.
 */
int
for_each_pool(int argc, char **argv, boolean_t unavail,
    zprop_list_t **proplist, zfs_type_t type, boolean_t literal,
    zpool_iter_f func, void *data)
{
	zpool_list_t *list;
	int ret = 0;

	if ((list = pool_list_get(argc, argv, proplist, type, literal,
	    &ret)) == NULL)
		return (1);

	if (pool_list_iter(list, unavail, func, data) != 0)
		ret = 1;

	pool_list_free(list);

	return (ret);
}

/*
 * This is the equivalent of for_each_pool() for vdevs.  It iterates thorough
 * all vdevs in the pool, ignoring root vdevs and holes, calling func() on
 * each one.
 *
 * @zhp:	Zpool handle
 * @func:	Function to call on each vdev
 * @data:	Custom data to pass to the function
 */
int
for_each_vdev(zpool_handle_t *zhp, pool_vdev_iter_f func, void *data)
{
	nvlist_t *config, *nvroot = NULL;

	if ((config = zpool_get_config(zhp, NULL)) != NULL) {
		verify(nvlist_lookup_nvlist(config, ZPOOL_CONFIG_VDEV_TREE,
		    &nvroot) == 0);
	}
	return (for_each_vdev_cb((void *) zhp, nvroot, func, data));
}

/*
 * Process the vcdl->vdev_cmd_data[] array to figure out all the unique column
 * names and their widths.  When this function is done, vcdl->uniq_cols,
 * vcdl->uniq_cols_cnt, and vcdl->uniq_cols_width will be filled in.
 */
static void
process_unique_cmd_columns(vdev_cmd_data_list_t *vcdl)
{
	char **uniq_cols = NULL, **tmp = NULL;
	int *uniq_cols_width;
	vdev_cmd_data_t *data;
	int cnt = 0;
	int k;

	/* For each vdev */
	for (int i = 0; i < vcdl->count; i++) {
		data = &vcdl->data[i];
		/* For each column the vdev reported */
		for (int j = 0; j < data->cols_cnt; j++) {
			/* Is this column in our list of unique column names? */
			for (k = 0; k < cnt; k++) {
				if (strcmp(data->cols[j], uniq_cols[k]) == 0)
					break; /* yes it is */
			}
			if (k == cnt) {
				/* No entry for column, add to list */
				tmp = realloc(uniq_cols, sizeof (*uniq_cols) *
				    (cnt + 1));
				if (tmp == NULL)
					break; /* Nothing we can do... */
				uniq_cols = tmp;
				uniq_cols[cnt] = data->cols[j];
				cnt++;
			}
		}
	}

	/*
	 * We now have a list of all the unique column names.  Figure out the
	 * max width of each column by looking at the column name and all its
	 * values.
	 */
	uniq_cols_width = safe_malloc(sizeof (*uniq_cols_width) * cnt);
	for (int i = 0; i < cnt; i++) {
		/* Start off with the column title's width */
		uniq_cols_width[i] = strlen(uniq_cols[i]);
		/* For each vdev */
		for (int j = 0; j < vcdl->count; j++) {
			/* For each of the vdev's values in a column */
			data = &vcdl->data[j];
			for (k = 0; k < data->cols_cnt; k++) {
				/* Does this vdev have a value for this col? */
				if (strcmp(data->cols[k], uniq_cols[i]) == 0) {
					/* Is the value width larger? */
					uniq_cols_width[i] =
					    MAX(uniq_cols_width[i],
					    strlen(data->lines[k]));
				}
			}
		}
	}

	vcdl->uniq_cols = uniq_cols;
	vcdl->uniq_cols_cnt = cnt;
	vcdl->uniq_cols_width = uniq_cols_width;
}


/*
 * Process a line of command output
 *
 * When running 'zpool iostat|status -c' the lines of output can either be
 * in the form of:
 *
 *	column_name=value
 *
 * Or just:
 *
 *	value
 *
 * Process the column_name (if any) and value.
 *
 * Returns 0 if line was processed, and there are more lines can still be
 * processed.
 *
 * Returns 1 if this was the last line to process, or error.
 */
static int
vdev_process_cmd_output(vdev_cmd_data_t *data, char *line)
{
	char *col = NULL;
	char *val = line;
	char *equals;
	char **tmp;

	if (line == NULL)
		return (1);

	equals = strchr(line, '=');
	if (equals != NULL) {
		/*
		 * We have a 'column=value' type line.  Split it into the
		 * column and value strings by turning the '=' into a '\0'.
		 */
		*equals = '\0';
		col = line;
		val = equals + 1;
	} else {
		val = line;
	}

	/* Do we already have a column by this name?  If so, skip it. */
	if (col != NULL) {
		for (int i = 0; i < data->cols_cnt; i++) {
			if (strcmp(col, data->cols[i]) == 0)
				return (0); /* Duplicate, skip */
		}
	}

	if (val != NULL) {
		tmp = realloc(data->lines,
		    (data->lines_cnt + 1) * sizeof (*data->lines));
		if (tmp == NULL)
			return (1);

		data->lines = tmp;
		data->lines[data->lines_cnt] = strdup(val);
		data->lines_cnt++;
	}

	if (col != NULL) {
		tmp = realloc(data->cols,
		    (data->cols_cnt + 1) * sizeof (*data->cols));
		if (tmp == NULL)
			return (1);

		data->cols = tmp;
		data->cols[data->cols_cnt] = strdup(col);
		data->cols_cnt++;
	}

	if (val != NULL && col == NULL)
		return (1);

	return (0);
}

/*
 * Run the cmd and store results in *data.
 */
static void
vdev_run_cmd(vdev_cmd_data_t *data, char *cmd)
{
	int rc;
	char *argv[2] = {cmd};
	char *env[5] = {(char *)"PATH=/bin:/sbin:/usr/bin:/usr/sbin"};
	char **lines = NULL;
	int lines_cnt = 0;
	int i;

	/* Setup our custom environment variables */
	rc = asprintf(&env[1], "VDEV_PATH=%s",
	    data->path ? data->path : "");
	if (rc == -1) {
		env[1] = NULL;
		goto out;
	}

	rc = asprintf(&env[2], "VDEV_UPATH=%s",
	    data->upath ? data->upath : "");
	if (rc == -1) {
		env[2] = NULL;
		goto out;
	}

	rc = asprintf(&env[3], "VDEV_ENC_SYSFS_PATH=%s",
	    data->vdev_enc_sysfs_path ?
	    data->vdev_enc_sysfs_path : "");
	if (rc == -1) {
		env[3] = NULL;
		goto out;
	}

	/* Run the command */
	rc = libzfs_run_process_get_stdout_nopath(cmd, argv, env, &lines,
	    &lines_cnt);
	if (rc != 0)
		goto out;

	/* Process the output we got */
	for (i = 0; i < lines_cnt; i++)
		if (vdev_process_cmd_output(data, lines[i]) != 0)
			break;

out:
	if (lines != NULL)
		libzfs_free_str_array(lines, lines_cnt);

	/* Start with i = 1 since env[0] was statically allocated */
	for (i = 1; i < ARRAY_SIZE(env); i++)
		free(env[i]);
}

/*
 * Generate the search path for zpool iostat/status -c scripts.
 * The string returned must be freed.
 */
char *
zpool_get_cmd_search_path(void)
{
	const char *env;
	char *sp = NULL;

	env = getenv("ZPOOL_SCRIPTS_PATH");
	if (env != NULL)
		return (strdup(env));

	env = getenv("HOME");
	if (env != NULL) {
		if (asprintf(&sp, "%s/.zpool.d:%s",
		    env, ZPOOL_SCRIPTS_DIR) != -1) {
			return (sp);
		}
	}

	if (asprintf(&sp, "%s", ZPOOL_SCRIPTS_DIR) != -1)
		return (sp);

	return (NULL);
}

/* Thread function run for each vdev */
static void
vdev_run_cmd_thread(void *cb_cmd_data)
{
	vdev_cmd_data_t *data = cb_cmd_data;
	char *cmd = NULL, *cmddup, *cmdrest;

	cmddup = strdup(data->cmd);
	if (cmddup == NULL)
		return;

	cmdrest = cmddup;
	while ((cmd = strtok_r(cmdrest, ",", &cmdrest))) {
		char *dir = NULL, *sp, *sprest;
		char fullpath[MAXPATHLEN];

		if (strchr(cmd, '/') != NULL)
			continue;

		sp = zpool_get_cmd_search_path();
		if (sp == NULL)
			continue;

		sprest = sp;
		while ((dir = strtok_r(sprest, ":", &sprest))) {
			if (snprintf(fullpath, sizeof (fullpath),
			    "%s/%s", dir, cmd) == -1)
				continue;

			if (access(fullpath, X_OK) == 0) {
				vdev_run_cmd(data, fullpath);
				break;
			}
		}
		free(sp);
	}
	free(cmddup);
}

/* For each vdev in the pool run a command */
static int
for_each_vdev_run_cb(void *zhp_data, nvlist_t *nv, void *cb_vcdl)
{
	vdev_cmd_data_list_t *vcdl = cb_vcdl;
	vdev_cmd_data_t *data;
	char *path = NULL;
	char *vname = NULL;
	char *vdev_enc_sysfs_path = NULL;
	int i, match = 0;
	zpool_handle_t *zhp = zhp_data;

	if (nvlist_lookup_string(nv, ZPOOL_CONFIG_PATH, &path) != 0)
		return (1);

	nvlist_lookup_string(nv, ZPOOL_CONFIG_VDEV_ENC_SYSFS_PATH,
	    &vdev_enc_sysfs_path);

	/* Spares show more than once if they're in use, so skip if exists */
	for (i = 0; i < vcdl->count; i++) {
		if ((strcmp(vcdl->data[i].path, path) == 0) &&
		    (strcmp(vcdl->data[i].pool, zpool_get_name(zhp)) == 0)) {
			/* vdev already exists, skip it */
			return (0);
		}
	}

	/* Check for selected vdevs here, if any */
	for (i = 0; i < vcdl->vdev_names_count; i++) {
		vname = zpool_vdev_name(g_zfs, zhp, nv, vcdl->cb_name_flags);
		if (strcmp(vcdl->vdev_names[i], vname) == 0) {
			free(vname);
			match = 1;
			break; /* match */
		}
		free(vname);
	}

	/* If we selected vdevs, and this isn't one of them, then bail out */
	if (!match && vcdl->vdev_names_count)
		return (0);

	/*
	 * Resize our array and add in the new element.
	 */
	if (!(vcdl->data = realloc(vcdl->data,
	    sizeof (*vcdl->data) * (vcdl->count + 1))))
		return (ENOMEM);	/* couldn't realloc */

	data = &vcdl->data[vcdl->count];

	data->pool = strdup(zpool_get_name(zhp));
	data->path = strdup(path);
	data->upath = zfs_get_underlying_path(path);
	data->cmd = vcdl->cmd;
	data->lines = data->cols = NULL;
	data->lines_cnt = data->cols_cnt = 0;
	if (vdev_enc_sysfs_path)
		data->vdev_enc_sysfs_path = strdup(vdev_enc_sysfs_path);
	else
		data->vdev_enc_sysfs_path = NULL;

	vcdl->count++;

	return (0);
}

/* Get the names and count of the vdevs */
static int
all_pools_for_each_vdev_gather_cb(zpool_handle_t *zhp, void *cb_vcdl)
{
	return (for_each_vdev(zhp, for_each_vdev_run_cb, cb_vcdl));
}

/*
 * Now that vcdl is populated with our complete list of vdevs, spawn
 * off the commands.
 */
static void
all_pools_for_each_vdev_run_vcdl(vdev_cmd_data_list_t *vcdl)
{
	tpool_t *t;

	t = tpool_create(1, 5 * sysconf(_SC_NPROCESSORS_ONLN), 0, NULL);
	if (t == NULL)
		return;

	/* Spawn off the command for each vdev */
	for (int i = 0; i < vcdl->count; i++) {
		(void) tpool_dispatch(t, vdev_run_cmd_thread,
		    (void *) &vcdl->data[i]);
	}

	/* Wait for threads to finish */
	tpool_wait(t);
	tpool_destroy(t);
}

/*
 * Run command 'cmd' on all vdevs in all pools in argv.  Saves the first line of
 * output from the command in vcdk->data[].line for all vdevs.  If you want
 * to run the command on only certain vdevs, fill in g_zfs, vdev_names,
 * vdev_names_count, and cb_name_flags.  Otherwise leave them as zero.
 *
 * Returns a vdev_cmd_data_list_t that must be freed with
 * free_vdev_cmd_data_list();
 */
vdev_cmd_data_list_t *
all_pools_for_each_vdev_run(int argc, char **argv, char *cmd,
    libzfs_handle_t *g_zfs, char **vdev_names, int vdev_names_count,
    int cb_name_flags)
{
	vdev_cmd_data_list_t *vcdl;
	vcdl = safe_malloc(sizeof (vdev_cmd_data_list_t));
	vcdl->cmd = cmd;

	vcdl->vdev_names = vdev_names;
	vcdl->vdev_names_count = vdev_names_count;
	vcdl->cb_name_flags = cb_name_flags;
	vcdl->g_zfs = g_zfs;

	/* Gather our list of all vdevs in all pools */
	for_each_pool(argc, argv, B_TRUE, NULL, ZFS_TYPE_POOL,
	    B_FALSE, all_pools_for_each_vdev_gather_cb, vcdl);

	/* Run command on all vdevs in all pools */
	all_pools_for_each_vdev_run_vcdl(vcdl);

	/*
	 * vcdl->data[] now contains all the column names and values for each
	 * vdev.  We need to process that into a master list of unique column
	 * names, and figure out the width of each column.
	 */
	process_unique_cmd_columns(vcdl);

	return (vcdl);
}

/*
 * Free the vdev_cmd_data_list_t created by all_pools_for_each_vdev_run()
 */
void
free_vdev_cmd_data_list(vdev_cmd_data_list_t *vcdl)
{
	free(vcdl->uniq_cols);
	free(vcdl->uniq_cols_width);

	for (int i = 0; i < vcdl->count; i++) {
		free(vcdl->data[i].path);
		free(vcdl->data[i].pool);
		free(vcdl->data[i].upath);

		for (int j = 0; j < vcdl->data[i].lines_cnt; j++)
			free(vcdl->data[i].lines[j]);

		free(vcdl->data[i].lines);

		for (int j = 0; j < vcdl->data[i].cols_cnt; j++)
			free(vcdl->data[i].cols[j]);

		free(vcdl->data[i].cols);
		free(vcdl->data[i].vdev_enc_sysfs_path);
	}
	free(vcdl->data);
	free(vcdl);
}
