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
 * Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright 2015 Nexenta Systems, Inc. All rights reserved.
 * Copyright (c) 2015, 2018 by Delphix. All rights reserved.
 * Copyright 2016 Joyent, Inc.
 * Copyright 2016 Igor Kozhukhov <ikozhukhov@gmail.com>
 * Copyright (c) 2020, Datto Inc. All rights reserved.
 */

/*
 * zfs diff support
 */
#include <ctype.h>
#include <errno.h>
#include <libintl.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stddef.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stropts.h>
#include <pthread.h>
#include <sys/zfs_ioctl.h>
#include <libzfs.h>
#include "libzfs_impl.h"
#include <sys/zfs_znode.h>

#define	ZDIFF_SNAPDIR		"/.zfs/snapshot/"
#define	ZDIFF_PREFIX		"zfs-diff-%d"

#define	ZDIFF_ADDED	'+'
#define	ZDIFF_MODIFIED	'M'
#define	ZDIFF_REMOVED	'-'
#define	ZDIFF_RENAMED	'R'

typedef struct path_mapper path_mapper_t;

typedef struct differ_info {
	libzfs_handle_t *hdl;
	char *fromsnap;
	char *frommnt;
	char *tosnap;
	char *tomnt;
	char *ds;
	char *dsmnt;
	char *tmpsnap;
	char errbuf[1024];
	boolean_t isclone;
	boolean_t scripted;
	boolean_t classify;
	boolean_t timestamped;
	int zerr;
	int cleanupfd;
	int datafd;
	int outputfd;
	FILE *ofp;
	path_mapper_t *frompm;
	path_mapper_t *topm;
} differ_info_t;

typedef struct path_part path_part_t;
struct path_part {
	avl_node_t	pp_node;
	uint64_t	pp_obj;
	uint64_t	pp_parent;
	char		pp_name[];
};

struct path_mapper {
	avl_tree_t	pm_tree;
	libzfs_handle_t	*pm_hdl;
	int		pm_pipes[2];
	int		pm_err;
	const char	*pm_fsname;
};

static int
read_bytes(int fd, void *data, size_t bytes)
{
	uint8_t *pdata = data;
	uint8_t *pend = &pdata[bytes];

	while (pdata < pend) {
		int cnt = read(fd, pdata, pend - pdata);
		if (cnt == 0)
			return ((pdata > (uint8_t *)data) ? EPIPE : ENOENT);
		pdata += cnt;
	}

	return (0);
}

static int
pp_compare(const void *x1, const void *x2)
{
	const path_part_t *pp1 = x1;
	const path_part_t *pp2 = x2;
	return (TREE_CMP(pp1->pp_obj, pp2->pp_obj));
}

static path_part_t *
pp_create(path_mapper_t *pm, uint64_t parent, uint64_t obj, const char *path)
{
	int partlen = strlen(path);
	path_part_t *pp = zfs_alloc(pm->pm_hdl,
	    offsetof(path_part_t, pp_name[partlen + 1]));

	pp->pp_obj = obj;
	pp->pp_parent = parent;
	(void) strcpy(pp->pp_name, path);

	return (pp);
}

static path_part_t *
pp_create_from_zpr(path_mapper_t *pm, uint64_t parent, zap_pair_record_t *zpr)
{
	return (pp_create(pm, parent, ZFS_DIRENT_OBJ(zpr->zpr_value),
	    zpr->zpr_key));
}

static void
pp_destroy(path_part_t *pp)
{
	free(pp);
}

static path_mapper_t *
pathm_create(libzfs_handle_t *hdl, const char *snap)
{
	path_mapper_t *pm = zfs_alloc(hdl, sizeof (path_mapper_t));

	pm->pm_hdl = hdl;
	avl_create(&pm->pm_tree, pp_compare, sizeof (path_part_t),
	    offsetof(path_part_t, pp_node));

	pm->pm_fsname = snap;

	return (pm);
}

static void
pathm_destroy(path_mapper_t *pm)
{
	path_part_t *pp;
	void *cookie = NULL;

	if (pm == NULL)
		return;

	while ((pp = avl_destroy_nodes(&pm->pm_tree, &cookie)))
		pp_destroy(pp);

	avl_destroy(&pm->pm_tree);
	free(pm);
}

typedef struct dir_thread_arg {
	path_mapper_t	*dta_pm;
	uint64_t	dta_parent;
	int		dta_err;
} dir_thread_arg_t;

static void *
pathm_dir_thread(void *arg)
{
	uint64_t count;
	dir_thread_arg_t *dta = arg;
	path_mapper_t *pm = dta->dta_pm;

	dta->dta_err = read_bytes(pm->pm_pipes[0], &count, sizeof (count));
	if (dta->dta_err != 0)
		goto done;

	for (uint64_t iter = 0; ; iter++) {
		path_part_t *pp;
		path_part_t search;
		avl_index_t where = 0;
		zap_pair_record_t zpr;

		dta->dta_err = read_bytes(pm->pm_pipes[0], &zpr, sizeof (zpr));
		if (dta->dta_err != 0) {
			if (dta->dta_err == ENOENT && iter == count)
				dta->dta_err = 0;
			break;
		}

		search.pp_obj = ZFS_DIRENT_OBJ(zpr.zpr_value);
		if ((pp = avl_find(&pm->pm_tree, &search, &where))) {
			ASSERT3U(pp->pp_parent, ==, dta->dta_parent);
			continue;
		}

		pp = pp_create_from_zpr(pm, dta->dta_parent, &zpr);
		if (pp == NULL) {
			dta->dta_err = ENOMEM;
			break;
		}

		avl_insert(&pm->pm_tree, pp, where);
	}

done:
	(void) close(pm->pm_pipes[0]);
	return (NULL);
}

static int
pathm_read_dir(path_mapper_t *pm, uint64_t parent)
{
	dir_thread_arg_t dta = { .dta_pm = pm, .dta_parent = parent };
	pthread_t tid;
	int err = 0;

	if (pipe(pm->pm_pipes))
		return (EPIPE);

	if (pthread_create(&tid, NULL, pathm_dir_thread, &dta)) {
		(void) close(pm->pm_pipes[0]);
		(void) close(pm->pm_pipes[1]);
		return (EZFS_THREADCREATEFAILED);
	}

	/* do the ioctl() */
	err = lzc_dump_zap(pm->pm_fsname, parent, pm->pm_pipes[1]);
	(void) close(pm->pm_pipes[1]);
	(void) pthread_join(tid, NULL);
	pm->pm_err = (err != 0) ? err : dta.dta_err;
	return (pm->pm_err);
}

static path_part_t *
pathm_lookup(path_mapper_t *pm, uint64_t obj, zfs_diff_stat_t *zds)
{
	avl_index_t where = 0;
	path_part_t search, *pp;

	search.pp_obj = obj;
	pp = avl_find(&pm->pm_tree, &search, &where);
	if (pp)
		return (pp);

	if (zds->zds_parent == obj) {
		pp = pp_create(pm, obj, obj, "");
		avl_insert(&pm->pm_tree, pp, where);
		return (pp);
	} else if ((zds->zds_flags & ZFS_XATTR) && S_ISDIR(zds->zs.zs_mode)) {
		pp = pp_create(pm, zds->zds_parent, obj, "<xattrdir>");
		avl_insert(&pm->pm_tree, pp, where);
		return (pp);
	}

	if (pathm_read_dir(pm, zds->zds_parent) != 0)
		return (NULL);

	return (avl_find(&pm->pm_tree, &search, NULL));
}

static char *
pathm_build_path(path_mapper_t *pm, path_part_t *pp, char *path)
{
	path_part_t search, *parent;

	if (pp->pp_obj == pp->pp_parent)
		return (path);

	search.pp_obj = pp->pp_parent;
	parent = avl_find(&pm->pm_tree, &search, NULL);
	if (parent == NULL) {
		zfs_diff_stat_t zds = { { 0 } };
		nvlist_t *nvl = NULL;

		if (lzc_diff_stats(pm->pm_fsname, pp->pp_parent, &nvl) != 0) {
			pm->pm_err = errno;
			nvlist_free(nvl);
			return (NULL);
		}

		/* only need some of the properties */
		zds.zs.zs_mode = fnvlist_lookup_uint64(nvl, "mode");
		zds.zds_parent = fnvlist_lookup_uint64(nvl, "parent");
		zds.zds_flags = fnvlist_lookup_uint64(nvl, "flags");
		nvlist_free(nvl);

		parent = pathm_lookup(pm, pp->pp_parent, &zds);
		if (parent == NULL)
			return (NULL);
	}

	path = pathm_build_path(pm, parent, path);
	path += sprintf(path, "/%s", pp->pp_name);

	return (path);
}

static int
pathm_obj_to_path(path_mapper_t *pm, uint64_t obj, zfs_diff_stat_t *zds,
    int err, char *pn)
{
	pn[0] = '\0';
	pm->pm_err = err;
	if (pm->pm_err == 0) {
		path_part_t *pp = pathm_lookup(pm, obj, zds);
		if (pp && pathm_build_path(pm, pp, pn))
			return (0);

		if (pm->pm_err == 0)
			pm->pm_err = ENOENT;
	}

	if (pm->pm_err == ESTALE) {
		(void) snprintf(pn, MAXPATHLEN, "(on_delete_queue)");
		return (0);
	}

	return (-1);
}

/*PRINTFLIKE2*/
static int
di_err(differ_info_t *di, int err, const char *fmt, ...)
{
	va_list ap;

	if (err != 0)
		di->zerr = err;
	ASSERT3U(di->zerr, !=, 0);
	va_start(ap, fmt);
	(void) vsnprintf(di->errbuf, sizeof (di->errbuf), fmt, ap);
	va_end(ap);
	return (-1);
}

static int
di_pathm_err(differ_info_t *di, path_mapper_t *pm, uint64_t obj)
{
	if (pm->pm_err == EPERM) {
		return (di_err(di, pm->pm_err, dgettext(TEXT_DOMAIN,
		    "The sys_config privilege or diff delegated permission is "
		    "needed\nto discover path names")));
	} else if (pm->pm_err == EACCES) {
		return (di_err(di, pm->pm_err, dgettext(TEXT_DOMAIN,
		    "Key must be loaded to discover path names")));
	} else {
		return (di_err(di, pm->pm_err, dgettext(TEXT_DOMAIN,
		    "Unable to determine path or stats for object %lld in %s"),
		    (longlong_t)obj, pm->pm_fsname));
	}
}

/*
 * stream_bytes
 *
 * Prints a file name out a character at a time.  If the character is
 * not in the range of what we consider "printable" ASCII, display it
 * as an escaped 4-digit octal value.  ASCII values less than a space
 * are all control characters and we declare the upper end as the
 * DELete character.  This also is the last 7-bit ASCII character.
 * We choose to treat all 8-bit ASCII as not printable for this
 * application.
 */
static void
stream_bytes(FILE *fp, const char *string)
{
	char c;

	while ((c = *string++) != '\0') {
		if (c > ' ' && c != '\\' && c < '\177') {
			(void) fprintf(fp, "%c", c);
		} else {
			(void) fprintf(fp, "\\%04o", (uint8_t)c);
		}
	}
}

static char
mode_to_type(mode_t mode)
{
	switch (mode & S_IFMT) {
	case S_IFBLK:
		return ('B');
	case S_IFCHR:
		return ('C');
	case S_IFDIR:
		return ('/');
#ifdef S_IFDOOR
	case S_IFDOOR:
		return ('>');
#endif
	case S_IFIFO:
		return ('|');
	case S_IFLNK:
		return ('@');
#ifdef S_IFPORT
	case S_IFPORT:
		return ('P');
#endif
	case S_IFSOCK:
		return ('=');
	case S_IFREG:
		return ('F');
	default:
		return ('?');
	}
}

static void
di_print_cmn(differ_info_t *di, const char *file)
{
	stream_bytes(di->ofp, di->dsmnt);
	stream_bytes(di->ofp, (file[0] != '\0') ? file : "/");
}

static void
di_print_prefix(differ_info_t *di, char type, const char *path,
    zfs_diff_stat_t *zds)
{
	if (di->timestamped)
		(void) fprintf(di->ofp, "%10lld.%09lld\t",
		    (longlong_t)zds->zs.zs_ctime[0],
		    (longlong_t)zds->zs.zs_ctime[1]);
	(void) fprintf(di->ofp, "%c\t", type);
	if (di->classify)
		(void) fprintf(di->ofp, "%c\t", mode_to_type(zds->zs.zs_mode));
	di_print_cmn(di, path);
}

static void
di_print_rename(differ_info_t *di, const char *old, const char *new,
    zfs_diff_stat_t *zds)
{
	di_print_prefix(di, ZDIFF_RENAMED, old, zds);
	(void) fprintf(di->ofp, di->scripted ? "\t" : " -> ");
	di_print_cmn(di, new);
	(void) fprintf(di->ofp, "\n");
}

static void
di_print_link_change(differ_info_t *di, int delta, const char *file,
    zfs_diff_stat_t *zds)
{
	di_print_prefix(di, ZDIFF_MODIFIED, file, zds);
	(void) fprintf(di->ofp, "\t(%+d)\n", delta);
}

static void
di_print_file(differ_info_t *di, char type, const char *file,
    zfs_diff_stat_t *zds)
{
	di_print_prefix(di, type, file, zds);
	(void) fprintf(di->ofp, "\n");
}

static int
di_write_inuse_diff(differ_info_t *di, dmu_diff_record_t *dr)
{
	uint64_t obj = dr->ddr_obj;
	path_mapper_t *frompm = di->frompm, *topm = di->topm;
	zfs_diff_stat_t *tzs = &dr->ddr_zds[0], *fzs = &dr->ddr_zds[1];
	mode_t fmode, tmode;
	char fpath[MAXPATHLEN], tpath[MAXPATHLEN];
	int ferr, terr, change;

	/*
	 * Check the from and to snapshots for info on the object. If
	 * we get ENOENT, then the object just didn't exist in that
	 * snapshot.  If we get ENOTSUP, then we tried to get
	 * info on a non-ZPL object, which we don't care about anyway.
	 */
	ferr = pathm_obj_to_path(frompm, obj, fzs, dr->ddr_err[1], fpath);
	if (ferr && frompm->pm_err != ENOENT && frompm->pm_err != ENOTSUP)
		return (di_pathm_err(di, frompm, obj));

	terr = pathm_obj_to_path(topm, obj, tzs, dr->ddr_err[0], tpath);
	if (terr && topm->pm_err != ENOENT && topm->pm_err != ENOTSUP)
		return (di_pathm_err(di, topm, obj));

	di->zerr = 0;

	/*
	 * Unallocated object sharing the same meta dnode block
	 */
	if (ferr && terr)
		return (0);

	fmode = fzs->zs.zs_mode & S_IFMT;
	tmode = tzs->zs.zs_mode & S_IFMT;
	if (fmode == S_IFDIR || tmode == S_IFDIR || fzs->zs.zs_links == 0 ||
	    tzs->zs.zs_links == 0)
		change = 0;
	else
		change = tzs->zs.zs_links - fzs->zs.zs_links;

	if (ferr) {
		if (change)
			di_print_link_change(di, change, tpath, tzs);
		else
			di_print_file(di, ZDIFF_ADDED, tpath, tzs);
	} else if (terr) {
		if (change)
			di_print_link_change(di, change, fpath, fzs);
		else
			di_print_file(di, ZDIFF_REMOVED, fpath, fzs);
	} else {
		if (fmode != tmode && fzs->zs.zs_gen == tzs->zs.zs_gen)
			tzs->zs.zs_gen++; /* Force a generational difference */

		/* Simple modification or no change */
		if (fzs->zs.zs_gen == tzs->zs.zs_gen) {
			/* No apparent changes.  Could we assert !this?  */
			if (fzs->zs.zs_ctime[0] == tzs->zs.zs_ctime[0] &&
			    fzs->zs.zs_ctime[1] == tzs->zs.zs_ctime[1])
				return (0);
			if (change) {
				di_print_link_change(di, change,
				    change > 0 ? fpath : tpath, tzs);
			} else if (strcmp(fpath, tpath) == 0) {
				di_print_file(di, ZDIFF_MODIFIED, fpath, tzs);
			} else {
				di_print_rename(di, fpath, tpath, tzs);
			}
		} else {
			/* file re-created or object re-used */
			di_print_file(di, ZDIFF_REMOVED, fpath, fzs);
			di_print_file(di, ZDIFF_ADDED, tpath, tzs);
		}
	}

	return (0);
}

static int
di_write_free_diff(differ_info_t *di, dmu_diff_record_t *dr)
{
	char path[MAXPATHLEN];
	zfs_diff_stat_t *zds = &dr->ddr_zds[0];

	if (dr->ddr_err[0] != 0) {
		return (di_err(di, dr->ddr_err[0], dgettext(TEXT_DOMAIN,
		    "next allocated object (> %lld) find failure"),
		    (longlong_t)dr->ddr_obj));
	}

	if (pathm_obj_to_path(di->frompm, dr->ddr_obj, zds, 0, path) != 0)
		return (di_pathm_err(di, di->frompm, dr->ddr_obj));

	di_print_file(di, ZDIFF_REMOVED, path, zds);

	return (0);
}

static void *
differ(void *arg)
{
	differ_info_t *di = arg;
	const offset_t size1 = offsetof(dmu_diff_record_t, ddr_zds[1]);
	const offset_t size2 = sizeof (dmu_diff_record_t) - size1;
	dmu_diff_record_t dr;
	int err = 0;

	di->ofp = fdopen(di->outputfd, "w");
	if (di->ofp == NULL) {
		(void) di_err(di, errno, strerror(errno));
		(void) close(di->datafd);
		return ((void *)-1);
	}

	for (;;) {
		err = read_bytes(di->datafd, &dr, size1);
		if (err == 0 && dr.ddr_type == DDR_IN_BOTH)
			err = read_bytes(di->datafd, &dr.ddr_zds[1], size2);
		if (err != 0) {
			if (err == EPIPE)
				di->zerr = EPIPE;
			break;
		}

		if (dr.ddr_type == DDR_IN_TO || dr.ddr_type == DDR_IN_BOTH)
			err = di_write_inuse_diff(di, &dr);
		else if (dr.ddr_type == DDR_IN_FROM)
			err = di_write_free_diff(di, &dr);
		else
			di->zerr = EPIPE;

		if (err || di->zerr)
			break;
	}

	(void) fclose(di->ofp);
	(void) close(di->datafd);
	if (err && err != ENOENT)
		return ((void *)-1);
	if (di->zerr) {
		ASSERT3U(di->zerr, ==, EPIPE);
		(void) di_err(di, EZFS_UNKNOWN, dgettext(TEXT_DOMAIN,
		    "bad data from diff IOCTL"));
		return ((void *)-1);
	}
	return ((void *)0);
}

static int
di_make_temp_snapshot(differ_info_t *di)
{
	zfs_cmd_t zc = {"\0"};

	(void) snprintf(zc.zc_value, sizeof (zc.zc_value),
	    ZDIFF_PREFIX, getpid());
	(void) strlcpy(zc.zc_name, di->ds, sizeof (zc.zc_name));
	zc.zc_cleanup_fd = di->cleanupfd;

	if (zfs_ioctl(di->hdl, ZFS_IOC_TMP_SNAPSHOT, &zc) != 0) {
		if (errno == EPERM) {
			return (zfs_error_fmt(di->hdl, EZFS_DIFF,
			    dgettext(TEXT_DOMAIN, "The diff delegated "
			    "permission is needed in order\nto create a "
			    "just-in-time snapshot for diffing\n")));
		} else {
			return (zfs_standard_error_fmt(di->hdl, errno,
			    dgettext(TEXT_DOMAIN, "Cannot create just-in-time "
			    "snapshot of '%s'"), zc.zc_name));
		}
	}

	di->tmpsnap = zfs_strdup(di->hdl, zc.zc_value);
	di->tosnap = zfs_asprintf(di->hdl, "%s@%s", di->ds, di->tmpsnap);
	return (0);
}

static int
di_get_snapshot_names(differ_info_t *di, const char *fromsnap,
    const char *tosnap)
{
	libzfs_handle_t *hdl = di->hdl;
	char *atptrf = NULL;
	char *atptrt = NULL;
	int fdslen, fsnlen;
	int tdslen, tsnlen;

	/*
	 * Can accept
	 *                                      fdslen fsnlen tdslen tsnlen
	 *       dataset@snap1
	 *    0. dataset@snap1 dataset@snap2      >0     >1     >0     >1
	 *    1. dataset@snap1 @snap2             >0     >1    ==0     >1
	 *    2. dataset@snap1 dataset            >0     >1     >0    ==0
	 *    3. @snap1 dataset@snap2            ==0     >1     >0     >1
	 *    4. @snap1 dataset                  ==0     >1     >0    ==0
	 */
	if (tosnap == NULL) {
		/* only a from snapshot given, must be valid */
		if (!zfs_validate_name(hdl, fromsnap, ZFS_TYPE_SNAPSHOT,
		    B_FALSE)) {
			return (zfs_error_fmt(hdl, EZFS_INVALIDNAME,
			    dgettext(TEXT_DOMAIN,
			    "Badly formed snapshot name %s"), fromsnap));
		}

		atptrf = strchr(fromsnap, '@');
		ASSERT(atptrf != NULL);
		fdslen = atptrf - fromsnap;

		di->fromsnap = zfs_strdup(hdl, fromsnap);
		di->ds = zfs_strdup(hdl, fromsnap);
		di->ds[fdslen] = '\0';

		/* the to snap will be a just-in-time snap of the head */
		return (di_make_temp_snapshot(di));
	}

	atptrf = strchr(fromsnap, '@');
	atptrt = strchr(tosnap, '@');
	fdslen = atptrf ? atptrf - fromsnap : strlen(fromsnap);
	tdslen = atptrt ? atptrt - tosnap : strlen(tosnap);
	fsnlen = strlen(fromsnap) - fdslen;	/* includes @ sign */
	tsnlen = strlen(tosnap) - tdslen;	/* includes @ sign */

	if (fsnlen <= 1 || tsnlen == 1 || (fdslen == 0 && tdslen == 0)) {
		return (zfs_error(hdl, EZFS_INVALIDNAME, dgettext(TEXT_DOMAIN,
		    "Unable to determine which snapshots to compare")));
	} else if ((fdslen > 0 && tdslen > 0) &&
	    ((tdslen != fdslen || strncmp(fromsnap, tosnap, fdslen) != 0))) {
		/*
		 * not the same dataset name, might be okay if
		 * tosnap is a clone of a fromsnap descendant.
		 */
		char origin[ZFS_MAX_DATASET_NAME_LEN];
		zprop_source_t src;
		zfs_handle_t *zhp;

		di->ds = zfs_alloc(di->hdl, tdslen + 1);
		(void) strncpy(di->ds, tosnap, tdslen);
		di->ds[tdslen] = '\0';

		zhp = zfs_open(hdl, di->ds, ZFS_TYPE_FILESYSTEM);
		while (zhp != NULL) {
			if (zfs_prop_get(zhp, ZFS_PROP_ORIGIN, origin,
			    sizeof (origin), &src, NULL, 0, B_FALSE) != 0) {
				(void) zfs_close(zhp);
				zhp = NULL;
				break;
			}
			if (strncmp(origin, fromsnap, fsnlen) == 0)
				break;

			(void) zfs_close(zhp);
			zhp = zfs_open(hdl, origin, ZFS_TYPE_FILESYSTEM);
		}

		if (zhp == NULL) {
			return (zfs_error(hdl, EZFS_INVALIDNAME,
			    dgettext(TEXT_DOMAIN,
			    "Not an earlier snapshot from the same fs")));
		} else {
			(void) zfs_close(zhp);
		}

		di->isclone = B_TRUE;
		di->fromsnap = zfs_strdup(hdl, fromsnap);
		if (tsnlen) {
			di->tosnap = zfs_strdup(hdl, tosnap);
		} else {
			return (di_make_temp_snapshot(di));
		}
	} else {
		int dslen = fdslen ? fdslen : tdslen;

		di->ds = zfs_alloc(hdl, dslen + 1);
		(void) strncpy(di->ds, fdslen ? fromsnap : tosnap, dslen);
		di->ds[dslen] = '\0';

		di->fromsnap = zfs_asprintf(hdl, "%s%s", di->ds, atptrf);
		if (tsnlen) {
			di->tosnap = zfs_asprintf(hdl, "%s%s", di->ds, atptrt);
		} else {
			return (di_make_temp_snapshot(di));
		}
	}
	return (0);
}

static int
di_get_mountpoint(differ_info_t *di, char *dsnm, char **mntpt)
{
	if (!is_mounted(di->hdl, dsnm, mntpt))
		return (zfs_error(di->hdl, EZFS_BADTYPE, dgettext(TEXT_DOMAIN,
		    "Cannot diff an unmounted snapshot")));

	/* Avoid a double slash at the beginning of root-mounted datasets */
	if (**mntpt == '/' && *(*mntpt + 1) == '\0')
		**mntpt = '\0';
	return (0);
}

static int
di_get_mountpoints(differ_info_t *di)
{
	char *strptr, *frommntpt;

	/*
	 * first get the mountpoint for the parent dataset
	 */
	if (di_get_mountpoint(di, di->ds, &di->dsmnt) != 0)
		return (-1);

	strptr = strchr(di->tosnap, '@');
	ASSERT3P(strptr, !=, NULL);
	di->tomnt = zfs_asprintf(di->hdl, "%s%s%s", di->dsmnt,
	    ZDIFF_SNAPDIR, ++strptr);

	strptr = strchr(di->fromsnap, '@');
	ASSERT3P(strptr, !=, NULL);

	frommntpt = di->dsmnt;
	if (di->isclone) {
		int err;

		*strptr = '\0';
		err = di_get_mountpoint(di, di->fromsnap, &frommntpt);
		*strptr = '@';
		if (err != 0)
			return (-1);
	}

	di->frommnt = zfs_asprintf(di->hdl, "%s%s%s", frommntpt,
	    ZDIFF_SNAPDIR, ++strptr);

	if (di->isclone)
		free(frommntpt);

	return (0);
}

static int
di_setup(differ_info_t *di, zfs_handle_t *zhp, int outfd, const char *fromsnap,
    const char *tosnap, int flags)
{
	di->hdl = zhp->zfs_hdl;

	di->scripted = (flags & ZFS_DIFF_PARSEABLE);
	di->classify = (flags & ZFS_DIFF_CLASSIFY);
	di->timestamped = (flags & ZFS_DIFF_TIMESTAMP);

	di->outputfd = outfd;

	di->cleanupfd = open(ZFS_DEV, O_RDWR);
	VERIFY(di->cleanupfd >= 0);

	if (di_get_snapshot_names(di, fromsnap, tosnap) != 0)
		return (-1);

	if (di_get_mountpoints(di) != 0)
		return (-1);

	di->frompm = pathm_create(di->hdl, di->fromsnap);
	di->topm = pathm_create(di->hdl, di->tosnap);

	return (0);
}

static void
di_teardown(differ_info_t *di)
{
	pathm_destroy(di->frompm);
	pathm_destroy(di->topm);
	free(di->ds);
	free(di->dsmnt);
	free(di->fromsnap);
	free(di->frommnt);
	free(di->tosnap);
	free(di->tmpsnap);
	free(di->tomnt);
	(void) close(di->cleanupfd);
}

static int
di_show_diffs(differ_info_t *di)
{
	pthread_t tid;
	int pipefd[2];
	int iocerr;

	if (pipe(pipefd)) {
		zfs_error_aux(di->hdl, strerror(errno));
		return (zfs_error(di->hdl, EZFS_PIPEFAILED,
		    dgettext(TEXT_DOMAIN, "zfs diff failed")));
	}
	di->datafd = pipefd[0];

	if (pthread_create(&tid, NULL, differ, di)) {
		zfs_error_aux(di->hdl, strerror(errno));
		(void) close(pipefd[0]);
		(void) close(pipefd[1]);
		return (zfs_error(di->hdl, EZFS_THREADCREATEFAILED,
		    dgettext(TEXT_DOMAIN, "zfs diff failed")));
	}

	iocerr = lzc_diff(di->tosnap, di->fromsnap, pipefd[1]);
	(void) close(pipefd[1]);
	if (iocerr == 0) {
		(void) pthread_join(tid, NULL);
		if (di->zerr != 0) {
			zfs_error_aux(di->hdl, strerror(di->zerr));
			return (zfs_error(di->hdl, EZFS_DIFF, di->errbuf));
		}
		return (0);
	}

	(void) pthread_cancel(tid);
	(void) pthread_join(tid, NULL);

	if (di->zerr != 0 && di->zerr != EPIPE) {
		zfs_error_aux(di->hdl, strerror(di->zerr));
		return (zfs_error(di->hdl, EZFS_DIFF, di->errbuf));
	}

	if (errno == EPERM) {
		zfs_error_aux(di->hdl, dgettext(TEXT_DOMAIN,
		    "\n   The sys_mount privilege or diff delegated "
		    "permission is needed\n   to execute the diff ioctl"));
	} else if (errno == EXDEV) {
		zfs_error_aux(di->hdl, dgettext(TEXT_DOMAIN,
		    "\n   Not an earlier snapshot from the same fs"));
	} else if (errno != EPIPE || di->zerr == 0) {
		zfs_error_aux(di->hdl, strerror(errno));
	}

	return (zfs_error(di->hdl, EZFS_DIFFDATA,
	    dgettext(TEXT_DOMAIN, "Unable to obtain diffs")));
}

int
zfs_show_diffs(zfs_handle_t *zhp, int outfd, const char *fromsnap,
    const char *tosnap, int flags)
{
	differ_info_t di = { 0 };
	int err;

	err = di_setup(&di, zhp, outfd, fromsnap, tosnap, flags);
	if (err == 0)
		err = di_show_diffs(&di);
	di_teardown(&di);
	return (err);
}
