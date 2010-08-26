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

#include <assert.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <libintl.h>
#include <libuutil.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <priv.h>
#include <pwd.h>
#include <zone.h>
#include <sys/fs/zfs.h>
#include <sys/stat.h>

#include <libzfs.h>

#include "zpool_util.h"
#include "zfs_comutil.h"

#include "statcommon.h"

static int zpool_do_create(int, char **);
static int zpool_do_destroy(int, char **);

static int zpool_do_add(int, char **);
static int zpool_do_remove(int, char **);

static int zpool_do_list(int, char **);
static int zpool_do_iostat(int, char **);
static int zpool_do_status(int, char **);

static int zpool_do_online(int, char **);
static int zpool_do_offline(int, char **);
static int zpool_do_clear(int, char **);

static int zpool_do_attach(int, char **);
static int zpool_do_detach(int, char **);
static int zpool_do_replace(int, char **);
static int zpool_do_split(int, char **);

static int zpool_do_scrub(int, char **);

static int zpool_do_import(int, char **);
static int zpool_do_export(int, char **);

static int zpool_do_upgrade(int, char **);

static int zpool_do_history(int, char **);

static int zpool_do_get(int, char **);
static int zpool_do_set(int, char **);

/*
 * These libumem hooks provide a reasonable set of defaults for the allocator's
 * debugging facilities.
 */

#ifdef DEBUG
const char *
_umem_debug_init(void)
{
	return ("default,verbose"); /* $UMEM_DEBUG setting */
}

const char *
_umem_logging_init(void)
{
	return ("fail,contents"); /* $UMEM_LOGGING setting */
}
#endif

typedef enum {
	HELP_ADD,
	HELP_ATTACH,
	HELP_CLEAR,
	HELP_CREATE,
	HELP_DESTROY,
	HELP_DETACH,
	HELP_EXPORT,
	HELP_HISTORY,
	HELP_IMPORT,
	HELP_IOSTAT,
	HELP_LIST,
	HELP_OFFLINE,
	HELP_ONLINE,
	HELP_REPLACE,
	HELP_REMOVE,
	HELP_SCRUB,
	HELP_STATUS,
	HELP_UPGRADE,
	HELP_GET,
	HELP_SET,
	HELP_SPLIT
} zpool_help_t;


typedef struct zpool_command {
	const char	*name;
	int		(*func)(int, char **);
	zpool_help_t	usage;
} zpool_command_t;

/*
 * Master command table.  Each ZFS command has a name, associated function, and
 * usage message.  The usage messages need to be internationalized, so we have
 * to have a function to return the usage message based on a command index.
 *
 * These commands are organized according to how they are displayed in the usage
 * message.  An empty command (one with a NULL name) indicates an empty line in
 * the generic usage message.
 */
static zpool_command_t command_table[] = {
	{ "create",	zpool_do_create,	HELP_CREATE		},
	{ "destroy",	zpool_do_destroy,	HELP_DESTROY		},
	{ NULL },
	{ "add",	zpool_do_add,		HELP_ADD		},
	{ "remove",	zpool_do_remove,	HELP_REMOVE		},
	{ NULL },
	{ "list",	zpool_do_list,		HELP_LIST		},
	{ "iostat",	zpool_do_iostat,	HELP_IOSTAT		},
	{ "status",	zpool_do_status,	HELP_STATUS		},
	{ NULL },
	{ "online",	zpool_do_online,	HELP_ONLINE		},
	{ "offline",	zpool_do_offline,	HELP_OFFLINE		},
	{ "clear",	zpool_do_clear,		HELP_CLEAR		},
	{ NULL },
	{ "attach",	zpool_do_attach,	HELP_ATTACH		},
	{ "detach",	zpool_do_detach,	HELP_DETACH		},
	{ "replace",	zpool_do_replace,	HELP_REPLACE		},
	{ "split",	zpool_do_split,		HELP_SPLIT		},
	{ NULL },
	{ "scrub",	zpool_do_scrub,		HELP_SCRUB		},
	{ NULL },
	{ "import",	zpool_do_import,	HELP_IMPORT		},
	{ "export",	zpool_do_export,	HELP_EXPORT		},
	{ "upgrade",	zpool_do_upgrade,	HELP_UPGRADE		},
	{ NULL },
	{ "history",	zpool_do_history,	HELP_HISTORY		},
	{ "get",	zpool_do_get,		HELP_GET		},
	{ "set",	zpool_do_set,		HELP_SET		},
};

#define	NCOMMAND	(sizeof (command_table) / sizeof (command_table[0]))

zpool_command_t *current_command;
static char history_str[HIS_MAX_RECORD_LEN];

static uint_t timestamp_fmt = NODATE;

static const char *
get_usage(zpool_help_t idx) {
	switch (idx) {
	case HELP_ADD:
		return (gettext("\tadd [-fn] <pool> <vdev> ...\n"));
	case HELP_ATTACH:
		return (gettext("\tattach [-f] <pool> <device> "
		    "<new-device>\n"));
	case HELP_CLEAR:
		return (gettext("\tclear [-nF] <pool> [device]\n"));
	case HELP_CREATE:
		return (gettext("\tcreate [-fn] [-o property=value] ... \n"
		    "\t    [-O file-system-property=value] ... \n"
		    "\t    [-m mountpoint] [-R root] <pool> <vdev> ...\n"));
	case HELP_DESTROY:
		return (gettext("\tdestroy [-f] <pool>\n"));
	case HELP_DETACH:
		return (gettext("\tdetach <pool> <device>\n"));
	case HELP_EXPORT:
		return (gettext("\texport [-f] <pool> ...\n"));
	case HELP_HISTORY:
		return (gettext("\thistory [-il] [<pool>] ...\n"));
	case HELP_IMPORT:
		return (gettext("\timport [-d dir] [-D]\n"
		    "\timport [-d dir | -c cachefile] [-F [-n]] <pool | id>\n"
		    "\timport [-o mntopts] [-o property=value] ... \n"
		    "\t    [-d dir | -c cachefile] [-D] [-f] [-m] [-N] "
		    "[-R root] [-F [-n]] -a\n"
		    "\timport [-o mntopts] [-o property=value] ... \n"
		    "\t    [-d dir | -c cachefile] [-D] [-f] [-m] [-N] "
		    "[-R root] [-F [-n]]\n"
		    "\t    <pool | id> [newpool]\n"));
	case HELP_IOSTAT:
		return (gettext("\tiostat [-v] [-T d|u] [pool] ... [interval "
		    "[count]]\n"));
	case HELP_LIST:
		return (gettext("\tlist [-H] [-o property[,...]] "
		    "[-T d|u] [pool] ... [interval [count]]\n"));
	case HELP_OFFLINE:
		return (gettext("\toffline [-t] <pool> <device> ...\n"));
	case HELP_ONLINE:
		return (gettext("\tonline <pool> <device> ...\n"));
	case HELP_REPLACE:
		return (gettext("\treplace [-f] <pool> <device> "
		    "[new-device]\n"));
	case HELP_REMOVE:
		return (gettext("\tremove <pool> <device> ...\n"));
	case HELP_SCRUB:
		return (gettext("\tscrub [-s] <pool> ...\n"));
	case HELP_STATUS:
		return (gettext("\tstatus [-vx] [-T d|u] [pool] ... [interval "
		    "[count]]\n"));
	case HELP_UPGRADE:
		return (gettext("\tupgrade\n"
		    "\tupgrade -v\n"
		    "\tupgrade [-V version] <-a | pool ...>\n"));
	case HELP_GET:
		return (gettext("\tget <\"all\" | property[,...]> "
		    "<pool> ...\n"));
	case HELP_SET:
		return (gettext("\tset <property=value> <pool> \n"));
	case HELP_SPLIT:
		return (gettext("\tsplit [-n] [-R altroot] [-o mntopts]\n"
		    "\t    [-o property=value] <pool> <newpool> "
		    "[<device> ...]\n"));
	}

	abort();
	/* NOTREACHED */
}


/*
 * Callback routine that will print out a pool property value.
 */
static int
print_prop_cb(int prop, void *cb)
{
	FILE *fp = cb;

	(void) fprintf(fp, "\t%-15s  ", zpool_prop_to_name(prop));

	if (zpool_prop_readonly(prop))
		(void) fprintf(fp, "  NO   ");
	else
		(void) fprintf(fp, " YES   ");

	if (zpool_prop_values(prop) == NULL)
		(void) fprintf(fp, "-\n");
	else
		(void) fprintf(fp, "%s\n", zpool_prop_values(prop));

	return (ZPROP_CONT);
}

/*
 * Display usage message.  If we're inside a command, display only the usage for
 * that command.  Otherwise, iterate over the entire command table and display
 * a complete usage message.
 */
void
usage(boolean_t requested)
{
	FILE *fp = requested ? stdout : stderr;

	if (current_command == NULL) {
		int i;

		(void) fprintf(fp, gettext("usage: zpool command args ...\n"));
		(void) fprintf(fp,
		    gettext("where 'command' is one of the following:\n\n"));

		for (i = 0; i < NCOMMAND; i++) {
			if (command_table[i].name == NULL)
				(void) fprintf(fp, "\n");
			else
				(void) fprintf(fp, "%s",
				    get_usage(command_table[i].usage));
		}
	} else {
		(void) fprintf(fp, gettext("usage:\n"));
		(void) fprintf(fp, "%s", get_usage(current_command->usage));
	}

	if (current_command != NULL &&
	    ((strcmp(current_command->name, "set") == 0) ||
	    (strcmp(current_command->name, "get") == 0) ||
	    (strcmp(current_command->name, "list") == 0))) {

		(void) fprintf(fp,
		    gettext("\nthe following properties are supported:\n"));

		(void) fprintf(fp, "\n\t%-15s  %s   %s\n\n",
		    "PROPERTY", "EDIT", "VALUES");

		/* Iterate over all properties */
		(void) zprop_iter(print_prop_cb, fp, B_FALSE, B_TRUE,
		    ZFS_TYPE_POOL);
	}

	/*
	 * See comments at end of main().
	 */
	if (getenv("ZFS_ABORT") != NULL) {
		(void) printf("dumping core by request\n");
		abort();
	}

	exit(requested ? 0 : 2);
}

void
print_vdev_tree(zpool_handle_t *zhp, const char *name, nvlist_t *nv, int indent,
    boolean_t print_logs)
{
	nvlist_t **child;
	uint_t c, children;
	char *vname;

	if (name != NULL)
		(void) printf("\t%*s%s\n", indent, "", name);

	if (nvlist_lookup_nvlist_array(nv, ZPOOL_CONFIG_CHILDREN,
	    &child, &children) != 0)
		return;

	for (c = 0; c < children; c++) {
		uint64_t is_log = B_FALSE;

		(void) nvlist_lookup_uint64(child[c], ZPOOL_CONFIG_IS_LOG,
		    &is_log);
		if ((is_log && !print_logs) || (!is_log && print_logs))
			continue;

		vname = zpool_vdev_name(g_zfs, zhp, child[c], B_FALSE);
		print_vdev_tree(zhp, vname, child[c], indent + 2,
		    B_FALSE);
		free(vname);
	}
}

/*
 * Add a property pair (name, string-value) into a property nvlist.
 */
static int
add_prop_list(const char *propname, char *propval, nvlist_t **props,
    boolean_t poolprop)
{
	zpool_prop_t prop = ZPROP_INVAL;
	zfs_prop_t fprop;
	nvlist_t *proplist;
	const char *normnm;
	char *strval;

	if (*props == NULL &&
	    nvlist_alloc(props, NV_UNIQUE_NAME, 0) != 0) {
		(void) fprintf(stderr,
		    gettext("internal error: out of memory\n"));
		return (1);
	}

	proplist = *props;

	if (poolprop) {
		if ((prop = zpool_name_to_prop(propname)) == ZPROP_INVAL) {
			(void) fprintf(stderr, gettext("property '%s' is "
			    "not a valid pool property\n"), propname);
			return (2);
		}
		normnm = zpool_prop_to_name(prop);
	} else {
		if ((fprop = zfs_name_to_prop(propname)) != ZPROP_INVAL) {
			normnm = zfs_prop_to_name(fprop);
		} else {
			normnm = propname;
		}
	}

	if (nvlist_lookup_string(proplist, normnm, &strval) == 0 &&
	    prop != ZPOOL_PROP_CACHEFILE) {
		(void) fprintf(stderr, gettext("property '%s' "
		    "specified multiple times\n"), propname);
		return (2);
	}

	if (nvlist_add_string(proplist, normnm, propval) != 0) {
		(void) fprintf(stderr, gettext("internal "
		    "error: out of memory\n"));
		return (1);
	}

	return (0);
}

/*
 * zpool add [-fn] <pool> <vdev> ...
 *
 *	-f	Force addition of devices, even if they appear in use
 *	-n	Do not add the devices, but display the resulting layout if
 *		they were to be added.
 *
 * Adds the given vdevs to 'pool'.  As with create, the bulk of this work is
 * handled by get_vdev_spec(), which constructs the nvlist needed to pass to
 * libzfs.
 */
int
zpool_do_add(int argc, char **argv)
{
	boolean_t force = B_FALSE;
	boolean_t dryrun = B_FALSE;
	int c;
	nvlist_t *nvroot;
	char *poolname;
	int ret;
	zpool_handle_t *zhp;
	nvlist_t *config;

	/* check options */
	while ((c = getopt(argc, argv, "fn")) != -1) {
		switch (c) {
		case 'f':
			force = B_TRUE;
			break;
		case 'n':
			dryrun = B_TRUE;
			break;
		case '?':
			(void) fprintf(stderr, gettext("invalid option '%c'\n"),
			    optopt);
			usage(B_FALSE);
		}
	}

	argc -= optind;
	argv += optind;

	/* get pool name and check number of arguments */
	if (argc < 1) {
		(void) fprintf(stderr, gettext("missing pool name argument\n"));
		usage(B_FALSE);
	}
	if (argc < 2) {
		(void) fprintf(stderr, gettext("missing vdev specification\n"));
		usage(B_FALSE);
	}

	poolname = argv[0];

	argc--;
	argv++;

	if ((zhp = zpool_open(g_zfs, poolname)) == NULL)
		return (1);

	if ((config = zpool_get_config(zhp, NULL)) == NULL) {
		(void) fprintf(stderr, gettext("pool '%s' is unavailable\n"),
		    poolname);
		zpool_close(zhp);
		return (1);
	}

	/* pass off to get_vdev_spec for processing */
	nvroot = make_root_vdev(zhp, force, !force, B_FALSE, dryrun,
	    argc, argv);
	if (nvroot == NULL) {
		zpool_close(zhp);
		return (1);
	}

	if (dryrun) {
		nvlist_t *poolnvroot;

		verify(nvlist_lookup_nvlist(config, ZPOOL_CONFIG_VDEV_TREE,
		    &poolnvroot) == 0);

		(void) printf(gettext("would update '%s' to the following "
		    "configuration:\n"), zpool_get_name(zhp));

		/* print original main pool and new tree */
		print_vdev_tree(zhp, poolname, poolnvroot, 0, B_FALSE);
		print_vdev_tree(zhp, NULL, nvroot, 0, B_FALSE);

		/* Do the same for the logs */
		if (num_logs(poolnvroot) > 0) {
			print_vdev_tree(zhp, "logs", poolnvroot, 0, B_TRUE);
			print_vdev_tree(zhp, NULL, nvroot, 0, B_TRUE);
		} else if (num_logs(nvroot) > 0) {
			print_vdev_tree(zhp, "logs", nvroot, 0, B_TRUE);
		}

		ret = 0;
	} else {
		ret = (zpool_add(zhp, nvroot) != 0);
	}

	nvlist_free(nvroot);
	zpool_close(zhp);

	return (ret);
}

/*
 * zpool remove  <pool> <vdev> ...
 *
 * Removes the given vdev from the pool.  Currently, this supports removing
 * spares, cache, and log devices from the pool.
 */
int
zpool_do_remove(int argc, char **argv)
{
	char *poolname;
	int i, ret = 0;
	zpool_handle_t *zhp;

	argc--;
	argv++;

	/* get pool name and check number of arguments */
	if (argc < 1) {
		(void) fprintf(stderr, gettext("missing pool name argument\n"));
		usage(B_FALSE);
	}
	if (argc < 2) {
		(void) fprintf(stderr, gettext("missing device\n"));
		usage(B_FALSE);
	}

	poolname = argv[0];

	if ((zhp = zpool_open(g_zfs, poolname)) == NULL)
		return (1);

	for (i = 1; i < argc; i++) {
		if (zpool_vdev_remove(zhp, argv[i]) != 0)
			ret = 1;
	}

	return (ret);
}

/*
 * zpool create [-fn] [-o property=value] ...
 *		[-O file-system-property=value] ...
 *		[-R root] [-m mountpoint] <pool> <dev> ...
 *
 *	-f	Force creation, even if devices appear in use
 *	-n	Do not create the pool, but display the resulting layout if it
 *		were to be created.
 *      -R	Create a pool under an alternate root
 *      -m	Set default mountpoint for the root dataset.  By default it's
 *      	'/<pool>'
 *	-o	Set property=value.
 *	-O	Set fsproperty=value in the pool's root file system
 *
 * Creates the named pool according to the given vdev specification.  The
 * bulk of the vdev processing is done in get_vdev_spec() in zpool_vdev.c.  Once
 * we get the nvlist back from get_vdev_spec(), we either print out the contents
 * (if '-n' was specified), or pass it to libzfs to do the creation.
 */
int
zpool_do_create(int argc, char **argv)
{
	boolean_t force = B_FALSE;
	boolean_t dryrun = B_FALSE;
	int c;
	nvlist_t *nvroot = NULL;
	char *poolname;
	int ret = 1;
	char *altroot = NULL;
	char *mountpoint = NULL;
	nvlist_t *fsprops = NULL;
	nvlist_t *props = NULL;
	char *propval;

	/* check options */
	while ((c = getopt(argc, argv, ":fnR:m:o:O:")) != -1) {
		switch (c) {
		case 'f':
			force = B_TRUE;
			break;
		case 'n':
			dryrun = B_TRUE;
			break;
		case 'R':
			altroot = optarg;
			if (add_prop_list(zpool_prop_to_name(
			    ZPOOL_PROP_ALTROOT), optarg, &props, B_TRUE))
				goto errout;
			if (nvlist_lookup_string(props,
			    zpool_prop_to_name(ZPOOL_PROP_CACHEFILE),
			    &propval) == 0)
				break;
			if (add_prop_list(zpool_prop_to_name(
			    ZPOOL_PROP_CACHEFILE), "none", &props, B_TRUE))
				goto errout;
			break;
		case 'm':
			mountpoint = optarg;
			break;
		case 'o':
			if ((propval = strchr(optarg, '=')) == NULL) {
				(void) fprintf(stderr, gettext("missing "
				    "'=' for -o option\n"));
				goto errout;
			}
			*propval = '\0';
			propval++;

			if (add_prop_list(optarg, propval, &props, B_TRUE))
				goto errout;
			break;
		case 'O':
			if ((propval = strchr(optarg, '=')) == NULL) {
				(void) fprintf(stderr, gettext("missing "
				    "'=' for -O option\n"));
				goto errout;
			}
			*propval = '\0';
			propval++;

			if (add_prop_list(optarg, propval, &fsprops, B_FALSE))
				goto errout;
			break;
		case ':':
			(void) fprintf(stderr, gettext("missing argument for "
			    "'%c' option\n"), optopt);
			goto badusage;
		case '?':
			(void) fprintf(stderr, gettext("invalid option '%c'\n"),
			    optopt);
			goto badusage;
		}
	}

	argc -= optind;
	argv += optind;

	/* get pool name and check number of arguments */
	if (argc < 1) {
		(void) fprintf(stderr, gettext("missing pool name argument\n"));
		goto badusage;
	}
	if (argc < 2) {
		(void) fprintf(stderr, gettext("missing vdev specification\n"));
		goto badusage;
	}

	poolname = argv[0];

	/*
	 * As a special case, check for use of '/' in the name, and direct the
	 * user to use 'zfs create' instead.
	 */
	if (strchr(poolname, '/') != NULL) {
		(void) fprintf(stderr, gettext("cannot create '%s': invalid "
		    "character '/' in pool name\n"), poolname);
		(void) fprintf(stderr, gettext("use 'zfs create' to "
		    "create a dataset\n"));
		goto errout;
	}

	/* pass off to get_vdev_spec for bulk processing */
	nvroot = make_root_vdev(NULL, force, !force, B_FALSE, dryrun,
	    argc - 1, argv + 1);
	if (nvroot == NULL)
		goto errout;

	/* make_root_vdev() allows 0 toplevel children if there are spares */
	if (!zfs_allocatable_devs(nvroot)) {
		(void) fprintf(stderr, gettext("invalid vdev "
		    "specification: at least one toplevel vdev must be "
		    "specified\n"));
		goto errout;
	}


	if (altroot != NULL && altroot[0] != '/') {
		(void) fprintf(stderr, gettext("invalid alternate root '%s': "
		    "must be an absolute path\n"), altroot);
		goto errout;
	}

	/*
	 * Check the validity of the mountpoint and direct the user to use the
	 * '-m' mountpoint option if it looks like its in use.
	 */
	if (mountpoint == NULL ||
	    (strcmp(mountpoint, ZFS_MOUNTPOINT_LEGACY) != 0 &&
	    strcmp(mountpoint, ZFS_MOUNTPOINT_NONE) != 0)) {
		char buf[MAXPATHLEN];
		DIR *dirp;

		if (mountpoint && mountpoint[0] != '/') {
			(void) fprintf(stderr, gettext("invalid mountpoint "
			    "'%s': must be an absolute path, 'legacy', or "
			    "'none'\n"), mountpoint);
			goto errout;
		}

		if (mountpoint == NULL) {
			if (altroot != NULL)
				(void) snprintf(buf, sizeof (buf), "%s/%s",
				    altroot, poolname);
			else
				(void) snprintf(buf, sizeof (buf), "/%s",
				    poolname);
		} else {
			if (altroot != NULL)
				(void) snprintf(buf, sizeof (buf), "%s%s",
				    altroot, mountpoint);
			else
				(void) snprintf(buf, sizeof (buf), "%s",
				    mountpoint);
		}

		if ((dirp = opendir(buf)) == NULL && errno != ENOENT) {
			(void) fprintf(stderr, gettext("mountpoint '%s' : "
			    "%s\n"), buf, strerror(errno));
			(void) fprintf(stderr, gettext("use '-m' "
			    "option to provide a different default\n"));
			goto errout;
		} else if (dirp) {
			int count = 0;

			while (count < 3 && readdir(dirp) != NULL)
				count++;
			(void) closedir(dirp);

			if (count > 2) {
				(void) fprintf(stderr, gettext("mountpoint "
				    "'%s' exists and is not empty\n"), buf);
				(void) fprintf(stderr, gettext("use '-m' "
				    "option to provide a "
				    "different default\n"));
				goto errout;
			}
		}
	}

	if (dryrun) {
		/*
		 * For a dry run invocation, print out a basic message and run
		 * through all the vdevs in the list and print out in an
		 * appropriate hierarchy.
		 */
		(void) printf(gettext("would create '%s' with the "
		    "following layout:\n\n"), poolname);

		print_vdev_tree(NULL, poolname, nvroot, 0, B_FALSE);
		if (num_logs(nvroot) > 0)
			print_vdev_tree(NULL, "logs", nvroot, 0, B_TRUE);

		ret = 0;
	} else {
		/*
		 * Hand off to libzfs.
		 */
		if (zpool_create(g_zfs, poolname,
		    nvroot, props, fsprops) == 0) {
			zfs_handle_t *pool = zfs_open(g_zfs, poolname,
			    ZFS_TYPE_FILESYSTEM);
			if (pool != NULL) {
				if (mountpoint != NULL)
					verify(zfs_prop_set(pool,
					    zfs_prop_to_name(
					    ZFS_PROP_MOUNTPOINT),
					    mountpoint) == 0);
				if (zfs_mount(pool, NULL, 0) == 0)
					ret = zfs_shareall(pool);
				zfs_close(pool);
			}
		} else if (libzfs_errno(g_zfs) == EZFS_INVALIDNAME) {
			(void) fprintf(stderr, gettext("pool name may have "
			    "been omitted\n"));
		}
	}

errout:
	nvlist_free(nvroot);
	nvlist_free(fsprops);
	nvlist_free(props);
	return (ret);
badusage:
	nvlist_free(fsprops);
	nvlist_free(props);
	usage(B_FALSE);
	return (2);
}

/*
 * zpool destroy <pool>
 *
 * 	-f	Forcefully unmount any datasets
 *
 * Destroy the given pool.  Automatically unmounts any datasets in the pool.
 */
int
zpool_do_destroy(int argc, char **argv)
{
	boolean_t force = B_FALSE;
	int c;
	char *pool;
	zpool_handle_t *zhp;
	int ret;

	/* check options */
	while ((c = getopt(argc, argv, "f")) != -1) {
		switch (c) {
		case 'f':
			force = B_TRUE;
			break;
		case '?':
			(void) fprintf(stderr, gettext("invalid option '%c'\n"),
			    optopt);
			usage(B_FALSE);
		}
	}

	argc -= optind;
	argv += optind;

	/* check arguments */
	if (argc < 1) {
		(void) fprintf(stderr, gettext("missing pool argument\n"));
		usage(B_FALSE);
	}
	if (argc > 1) {
		(void) fprintf(stderr, gettext("too many arguments\n"));
		usage(B_FALSE);
	}

	pool = argv[0];

	if ((zhp = zpool_open_canfail(g_zfs, pool)) == NULL) {
		/*
		 * As a special case, check for use of '/' in the name, and
		 * direct the user to use 'zfs destroy' instead.
		 */
		if (strchr(pool, '/') != NULL)
			(void) fprintf(stderr, gettext("use 'zfs destroy' to "
			    "destroy a dataset\n"));
		return (1);
	}

	if (zpool_disable_datasets(zhp, force) != 0) {
		(void) fprintf(stderr, gettext("could not destroy '%s': "
		    "could not unmount datasets\n"), zpool_get_name(zhp));
		return (1);
	}

	ret = (zpool_destroy(zhp) != 0);

	zpool_close(zhp);

	return (ret);
}

/*
 * zpool export [-f] <pool> ...
 *
 *	-f	Forcefully unmount datasets
 *
 * Export the given pools.  By default, the command will attempt to cleanly
 * unmount any active datasets within the pool.  If the '-f' flag is specified,
 * then the datasets will be forcefully unmounted.
 */
int
zpool_do_export(int argc, char **argv)
{
	boolean_t force = B_FALSE;
	boolean_t hardforce = B_FALSE;
	int c;
	zpool_handle_t *zhp;
	int ret;
	int i;

	/* check options */
	while ((c = getopt(argc, argv, "fF")) != -1) {
		switch (c) {
		case 'f':
			force = B_TRUE;
			break;
		case 'F':
			hardforce = B_TRUE;
			break;
		case '?':
			(void) fprintf(stderr, gettext("invalid option '%c'\n"),
			    optopt);
			usage(B_FALSE);
		}
	}

	argc -= optind;
	argv += optind;

	/* check arguments */
	if (argc < 1) {
		(void) fprintf(stderr, gettext("missing pool argument\n"));
		usage(B_FALSE);
	}

	ret = 0;
	for (i = 0; i < argc; i++) {
		if ((zhp = zpool_open_canfail(g_zfs, argv[i])) == NULL) {
			ret = 1;
			continue;
		}

		if (zpool_disable_datasets(zhp, force) != 0) {
			ret = 1;
			zpool_close(zhp);
			continue;
		}

		if (hardforce) {
			if (zpool_export_force(zhp) != 0)
				ret = 1;
		} else if (zpool_export(zhp, force) != 0) {
			ret = 1;
		}

		zpool_close(zhp);
	}

	return (ret);
}

/*
 * Given a vdev configuration, determine the maximum width needed for the device
 * name column.
 */
static int
max_width(zpool_handle_t *zhp, nvlist_t *nv, int depth, int max)
{
	char *name = zpool_vdev_name(g_zfs, zhp, nv, B_TRUE);
	nvlist_t **child;
	uint_t c, children;
	int ret;

	if (strlen(name) + depth > max)
		max = strlen(name) + depth;

	free(name);

	if (nvlist_lookup_nvlist_array(nv, ZPOOL_CONFIG_SPARES,
	    &child, &children) == 0) {
		for (c = 0; c < children; c++)
			if ((ret = max_width(zhp, child[c], depth + 2,
			    max)) > max)
				max = ret;
	}

	if (nvlist_lookup_nvlist_array(nv, ZPOOL_CONFIG_L2CACHE,
	    &child, &children) == 0) {
		for (c = 0; c < children; c++)
			if ((ret = max_width(zhp, child[c], depth + 2,
			    max)) > max)
				max = ret;
	}

	if (nvlist_lookup_nvlist_array(nv, ZPOOL_CONFIG_CHILDREN,
	    &child, &children) == 0) {
		for (c = 0; c < children; c++)
			if ((ret = max_width(zhp, child[c], depth + 2,
			    max)) > max)
				max = ret;
	}


	return (max);
}

typedef struct spare_cbdata {
	uint64_t	cb_guid;
	zpool_handle_t	*cb_zhp;
} spare_cbdata_t;

static boolean_t
find_vdev(nvlist_t *nv, uint64_t search)
{
	uint64_t guid;
	nvlist_t **child;
	uint_t c, children;

	if (nvlist_lookup_uint64(nv, ZPOOL_CONFIG_GUID, &guid) == 0 &&
	    search == guid)
		return (B_TRUE);

	if (nvlist_lookup_nvlist_array(nv, ZPOOL_CONFIG_CHILDREN,
	    &child, &children) == 0) {
		for (c = 0; c < children; c++)
			if (find_vdev(child[c], search))
				return (B_TRUE);
	}

	return (B_FALSE);
}

static int
find_spare(zpool_handle_t *zhp, void *data)
{
	spare_cbdata_t *cbp = data;
	nvlist_t *config, *nvroot;

	config = zpool_get_config(zhp, NULL);
	verify(nvlist_lookup_nvlist(config, ZPOOL_CONFIG_VDEV_TREE,
	    &nvroot) == 0);

	if (find_vdev(nvroot, cbp->cb_guid)) {
		cbp->cb_zhp = zhp;
		return (1);
	}

	zpool_close(zhp);
	return (0);
}

/*
 * Print out configuration state as requested by status_callback.
 */
void
print_status_config(zpool_handle_t *zhp, const char *name, nvlist_t *nv,
    int namewidth, int depth, boolean_t isspare)
{
	nvlist_t **child;
	uint_t c, children;
	pool_scan_stat_t *ps = NULL;
	vdev_stat_t *vs;
	char rbuf[6], wbuf[6], cbuf[6];
	char *vname;
	uint64_t notpresent;
	spare_cbdata_t cb;
	char *state;

	if (nvlist_lookup_nvlist_array(nv, ZPOOL_CONFIG_CHILDREN,
	    &child, &children) != 0)
		children = 0;

	verify(nvlist_lookup_uint64_array(nv, ZPOOL_CONFIG_VDEV_STATS,
	    (uint64_t **)&vs, &c) == 0);

	state = zpool_state_to_name(vs->vs_state, vs->vs_aux);
	if (isspare) {
		/*
		 * For hot spares, we use the terms 'INUSE' and 'AVAILABLE' for
		 * online drives.
		 */
		if (vs->vs_aux == VDEV_AUX_SPARED)
			state = "INUSE";
		else if (vs->vs_state == VDEV_STATE_HEALTHY)
			state = "AVAIL";
	}

	(void) printf("\t%*s%-*s  %-8s", depth, "", namewidth - depth,
	    name, state);

	if (!isspare) {
		zfs_nicenum(vs->vs_read_errors, rbuf, sizeof (rbuf));
		zfs_nicenum(vs->vs_write_errors, wbuf, sizeof (wbuf));
		zfs_nicenum(vs->vs_checksum_errors, cbuf, sizeof (cbuf));
		(void) printf(" %5s %5s %5s", rbuf, wbuf, cbuf);
	}

	if (nvlist_lookup_uint64(nv, ZPOOL_CONFIG_NOT_PRESENT,
	    &notpresent) == 0) {
		char *path;
		verify(nvlist_lookup_string(nv, ZPOOL_CONFIG_PATH, &path) == 0);
		(void) printf("  was %s", path);
	} else if (vs->vs_aux != 0) {
		(void) printf("  ");

		switch (vs->vs_aux) {
		case VDEV_AUX_OPEN_FAILED:
			(void) printf(gettext("cannot open"));
			break;

		case VDEV_AUX_BAD_GUID_SUM:
			(void) printf(gettext("missing device"));
			break;

		case VDEV_AUX_NO_REPLICAS:
			(void) printf(gettext("insufficient replicas"));
			break;

		case VDEV_AUX_VERSION_NEWER:
			(void) printf(gettext("newer version"));
			break;

		case VDEV_AUX_SPARED:
			verify(nvlist_lookup_uint64(nv, ZPOOL_CONFIG_GUID,
			    &cb.cb_guid) == 0);
			if (zpool_iter(g_zfs, find_spare, &cb) == 1) {
				if (strcmp(zpool_get_name(cb.cb_zhp),
				    zpool_get_name(zhp)) == 0)
					(void) printf(gettext("currently in "
					    "use"));
				else
					(void) printf(gettext("in use by "
					    "pool '%s'"),
					    zpool_get_name(cb.cb_zhp));
				zpool_close(cb.cb_zhp);
			} else {
				(void) printf(gettext("currently in use"));
			}
			break;

		case VDEV_AUX_ERR_EXCEEDED:
			(void) printf(gettext("too many errors"));
			break;

		case VDEV_AUX_IO_FAILURE:
			(void) printf(gettext("experienced I/O failures"));
			break;

		case VDEV_AUX_BAD_LOG:
			(void) printf(gettext("bad intent log"));
			break;

		case VDEV_AUX_EXTERNAL:
			(void) printf(gettext("external device fault"));
			break;

		case VDEV_AUX_SPLIT_POOL:
			(void) printf(gettext("split into new pool"));
			break;

		default:
			(void) printf(gettext("corrupted data"));
			break;
		}
	}

	(void) nvlist_lookup_uint64_array(nv, ZPOOL_CONFIG_SCAN_STATS,
	    (uint64_t **)&ps, &c);

	if (ps && ps->pss_state == DSS_SCANNING &&
	    vs->vs_scan_processed != 0 && children == 0) {
		(void) printf(gettext("  (%s)"),
		    (ps->pss_func == POOL_SCAN_RESILVER) ?
		    "resilvering" : "repairing");
	}

	(void) printf("\n");

	for (c = 0; c < children; c++) {
		uint64_t islog = B_FALSE, ishole = B_FALSE;

		/* Don't print logs or holes here */
		(void) nvlist_lookup_uint64(child[c], ZPOOL_CONFIG_IS_LOG,
		    &islog);
		(void) nvlist_lookup_uint64(child[c], ZPOOL_CONFIG_IS_HOLE,
		    &ishole);
		if (islog || ishole)
			continue;
		vname = zpool_vdev_name(g_zfs, zhp, child[c], B_TRUE);
		print_status_config(zhp, vname, child[c],
		    namewidth, depth + 2, isspare);
		free(vname);
	}
}


/*
 * Print the configuration of an exported pool.  Iterate over all vdevs in the
 * pool, printing out the name and status for each one.
 */
void
print_import_config(const char *name, nvlist_t *nv, int namewidth, int depth)
{
	nvlist_t **child;
	uint_t c, children;
	vdev_stat_t *vs;
	char *type, *vname;

	verify(nvlist_lookup_string(nv, ZPOOL_CONFIG_TYPE, &type) == 0);
	if (strcmp(type, VDEV_TYPE_MISSING) == 0 ||
	    strcmp(type, VDEV_TYPE_HOLE) == 0)
		return;

	verify(nvlist_lookup_uint64_array(nv, ZPOOL_CONFIG_VDEV_STATS,
	    (uint64_t **)&vs, &c) == 0);

	(void) printf("\t%*s%-*s", depth, "", namewidth - depth, name);
	(void) printf("  %s", zpool_state_to_name(vs->vs_state, vs->vs_aux));

	if (vs->vs_aux != 0) {
		(void) printf("  ");

		switch (vs->vs_aux) {
		case VDEV_AUX_OPEN_FAILED:
			(void) printf(gettext("cannot open"));
			break;

		case VDEV_AUX_BAD_GUID_SUM:
			(void) printf(gettext("missing device"));
			break;

		case VDEV_AUX_NO_REPLICAS:
			(void) printf(gettext("insufficient replicas"));
			break;

		case VDEV_AUX_VERSION_NEWER:
			(void) printf(gettext("newer version"));
			break;

		case VDEV_AUX_ERR_EXCEEDED:
			(void) printf(gettext("too many errors"));
			break;

		default:
			(void) printf(gettext("corrupted data"));
			break;
		}
	}
	(void) printf("\n");

	if (nvlist_lookup_nvlist_array(nv, ZPOOL_CONFIG_CHILDREN,
	    &child, &children) != 0)
		return;

	for (c = 0; c < children; c++) {
		uint64_t is_log = B_FALSE;

		(void) nvlist_lookup_uint64(child[c], ZPOOL_CONFIG_IS_LOG,
		    &is_log);
		if (is_log)
			continue;

		vname = zpool_vdev_name(g_zfs, NULL, child[c], B_TRUE);
		print_import_config(vname, child[c], namewidth, depth + 2);
		free(vname);
	}

	if (nvlist_lookup_nvlist_array(nv, ZPOOL_CONFIG_L2CACHE,
	    &child, &children) == 0) {
		(void) printf(gettext("\tcache\n"));
		for (c = 0; c < children; c++) {
			vname = zpool_vdev_name(g_zfs, NULL, child[c], B_FALSE);
			(void) printf("\t  %s\n", vname);
			free(vname);
		}
	}

	if (nvlist_lookup_nvlist_array(nv, ZPOOL_CONFIG_SPARES,
	    &child, &children) == 0) {
		(void) printf(gettext("\tspares\n"));
		for (c = 0; c < children; c++) {
			vname = zpool_vdev_name(g_zfs, NULL, child[c], B_FALSE);
			(void) printf("\t  %s\n", vname);
			free(vname);
		}
	}
}

/*
 * Print log vdevs.
 * Logs are recorded as top level vdevs in the main pool child array
 * but with "is_log" set to 1. We use either print_status_config() or
 * print_import_config() to print the top level logs then any log
 * children (eg mirrored slogs) are printed recursively - which
 * works because only the top level vdev is marked "is_log"
 */
static void
print_logs(zpool_handle_t *zhp, nvlist_t *nv, int namewidth, boolean_t verbose)
{
	uint_t c, children;
	nvlist_t **child;

	if (nvlist_lookup_nvlist_array(nv, ZPOOL_CONFIG_CHILDREN, &child,
	    &children) != 0)
		return;

	(void) printf(gettext("\tlogs\n"));

	for (c = 0; c < children; c++) {
		uint64_t is_log = B_FALSE;
		char *name;

		(void) nvlist_lookup_uint64(child[c], ZPOOL_CONFIG_IS_LOG,
		    &is_log);
		if (!is_log)
			continue;
		name = zpool_vdev_name(g_zfs, zhp, child[c], B_TRUE);
		if (verbose)
			print_status_config(zhp, name, child[c], namewidth,
			    2, B_FALSE);
		else
			print_import_config(name, child[c], namewidth, 2);
		free(name);
	}
}

/*
 * Display the status for the given pool.
 */
static void
show_import(nvlist_t *config)
{
	uint64_t pool_state;
	vdev_stat_t *vs;
	char *name;
	uint64_t guid;
	char *msgid;
	nvlist_t *nvroot;
	int reason;
	const char *health;
	uint_t vsc;
	int namewidth;

	verify(nvlist_lookup_string(config, ZPOOL_CONFIG_POOL_NAME,
	    &name) == 0);
	verify(nvlist_lookup_uint64(config, ZPOOL_CONFIG_POOL_GUID,
	    &guid) == 0);
	verify(nvlist_lookup_uint64(config, ZPOOL_CONFIG_POOL_STATE,
	    &pool_state) == 0);
	verify(nvlist_lookup_nvlist(config, ZPOOL_CONFIG_VDEV_TREE,
	    &nvroot) == 0);

	verify(nvlist_lookup_uint64_array(nvroot, ZPOOL_CONFIG_VDEV_STATS,
	    (uint64_t **)&vs, &vsc) == 0);
	health = zpool_state_to_name(vs->vs_state, vs->vs_aux);

	reason = zpool_import_status(config, &msgid);

	(void) printf(gettext("  pool: %s\n"), name);
	(void) printf(gettext("    id: %llu\n"), (u_longlong_t)guid);
	(void) printf(gettext(" state: %s"), health);
	if (pool_state == POOL_STATE_DESTROYED)
		(void) printf(gettext(" (DESTROYED)"));
	(void) printf("\n");

	switch (reason) {
	case ZPOOL_STATUS_MISSING_DEV_R:
	case ZPOOL_STATUS_MISSING_DEV_NR:
	case ZPOOL_STATUS_BAD_GUID_SUM:
		(void) printf(gettext("status: One or more devices are missing "
		    "from the system.\n"));
		break;

	case ZPOOL_STATUS_CORRUPT_LABEL_R:
	case ZPOOL_STATUS_CORRUPT_LABEL_NR:
		(void) printf(gettext("status: One or more devices contains "
		    "corrupted data.\n"));
		break;

	case ZPOOL_STATUS_CORRUPT_DATA:
		(void) printf(gettext("status: The pool data is corrupted.\n"));
		break;

	case ZPOOL_STATUS_OFFLINE_DEV:
		(void) printf(gettext("status: One or more devices "
		    "are offlined.\n"));
		break;

	case ZPOOL_STATUS_CORRUPT_POOL:
		(void) printf(gettext("status: The pool metadata is "
		    "corrupted.\n"));
		break;

	case ZPOOL_STATUS_VERSION_OLDER:
		(void) printf(gettext("status: The pool is formatted using an "
		    "older on-disk version.\n"));
		break;

	case ZPOOL_STATUS_VERSION_NEWER:
		(void) printf(gettext("status: The pool is formatted using an "
		    "incompatible version.\n"));
		break;

	case ZPOOL_STATUS_HOSTID_MISMATCH:
		(void) printf(gettext("status: The pool was last accessed by "
		    "another system.\n"));
		break;

	case ZPOOL_STATUS_FAULTED_DEV_R:
	case ZPOOL_STATUS_FAULTED_DEV_NR:
		(void) printf(gettext("status: One or more devices are "
		    "faulted.\n"));
		break;

	case ZPOOL_STATUS_BAD_LOG:
		(void) printf(gettext("status: An intent log record cannot be "
		    "read.\n"));
		break;

	case ZPOOL_STATUS_RESILVERING:
		(void) printf(gettext("status: One or more devices were being "
		    "resilvered.\n"));
		break;

	default:
		/*
		 * No other status can be seen when importing pools.
		 */
		assert(reason == ZPOOL_STATUS_OK);
	}

	/*
	 * Print out an action according to the overall state of the pool.
	 */
	if (vs->vs_state == VDEV_STATE_HEALTHY) {
		if (reason == ZPOOL_STATUS_VERSION_OLDER)
			(void) printf(gettext("action: The pool can be "
			    "imported using its name or numeric identifier, "
			    "though\n\tsome features will not be available "
			    "without an explicit 'zpool upgrade'.\n"));
		else if (reason == ZPOOL_STATUS_HOSTID_MISMATCH)
			(void) printf(gettext("action: The pool can be "
			    "imported using its name or numeric "
			    "identifier and\n\tthe '-f' flag.\n"));
		else
			(void) printf(gettext("action: The pool can be "
			    "imported using its name or numeric "
			    "identifier.\n"));
	} else if (vs->vs_state == VDEV_STATE_DEGRADED) {
		(void) printf(gettext("action: The pool can be imported "
		    "despite missing or damaged devices.  The\n\tfault "
		    "tolerance of the pool may be compromised if imported.\n"));
	} else {
		switch (reason) {
		case ZPOOL_STATUS_VERSION_NEWER:
			(void) printf(gettext("action: The pool cannot be "
			    "imported.  Access the pool on a system running "
			    "newer\n\tsoftware, or recreate the pool from "
			    "backup.\n"));
			break;
		case ZPOOL_STATUS_MISSING_DEV_R:
		case ZPOOL_STATUS_MISSING_DEV_NR:
		case ZPOOL_STATUS_BAD_GUID_SUM:
			(void) printf(gettext("action: The pool cannot be "
			    "imported. Attach the missing\n\tdevices and try "
			    "again.\n"));
			break;
		default:
			(void) printf(gettext("action: The pool cannot be "
			    "imported due to damaged devices or data.\n"));
		}
	}

	/*
	 * If the state is "closed" or "can't open", and the aux state
	 * is "corrupt data":
	 */
	if (((vs->vs_state == VDEV_STATE_CLOSED) ||
	    (vs->vs_state == VDEV_STATE_CANT_OPEN)) &&
	    (vs->vs_aux == VDEV_AUX_CORRUPT_DATA)) {
		if (pool_state == POOL_STATE_DESTROYED)
			(void) printf(gettext("\tThe pool was destroyed, "
			    "but can be imported using the '-Df' flags.\n"));
		else if (pool_state != POOL_STATE_EXPORTED)
			(void) printf(gettext("\tThe pool may be active on "
			    "another system, but can be imported using\n\t"
			    "the '-f' flag.\n"));
	}

	if (msgid != NULL)
		(void) printf(gettext("   see: http://www.sun.com/msg/%s\n"),
		    msgid);

	(void) printf(gettext("config:\n\n"));

	namewidth = max_width(NULL, nvroot, 0, 0);
	if (namewidth < 10)
		namewidth = 10;

	print_import_config(name, nvroot, namewidth, 0);
	if (num_logs(nvroot) > 0)
		print_logs(NULL, nvroot, namewidth, B_FALSE);

	if (reason == ZPOOL_STATUS_BAD_GUID_SUM) {
		(void) printf(gettext("\n\tAdditional devices are known to "
		    "be part of this pool, though their\n\texact "
		    "configuration cannot be determined.\n"));
	}
}

/*
 * Perform the import for the given configuration.  This passes the heavy
 * lifting off to zpool_import_props(), and then mounts the datasets contained
 * within the pool.
 */
static int
do_import(nvlist_t *config, const char *newname, const char *mntopts,
    nvlist_t *props, int flags)
{
	zpool_handle_t *zhp;
	char *name;
	uint64_t state;
	uint64_t version;

	verify(nvlist_lookup_string(config, ZPOOL_CONFIG_POOL_NAME,
	    &name) == 0);

	verify(nvlist_lookup_uint64(config,
	    ZPOOL_CONFIG_POOL_STATE, &state) == 0);
	verify(nvlist_lookup_uint64(config,
	    ZPOOL_CONFIG_VERSION, &version) == 0);
	if (version > SPA_VERSION) {
		(void) fprintf(stderr, gettext("cannot import '%s': pool "
		    "is formatted using a newer ZFS version\n"), name);
		return (1);
	} else if (state != POOL_STATE_EXPORTED &&
	    !(flags & ZFS_IMPORT_ANY_HOST)) {
		uint64_t hostid;

		if (nvlist_lookup_uint64(config, ZPOOL_CONFIG_HOSTID,
		    &hostid) == 0) {
			if ((unsigned long)hostid != gethostid()) {
				char *hostname;
				uint64_t timestamp;
				time_t t;

				verify(nvlist_lookup_string(config,
				    ZPOOL_CONFIG_HOSTNAME, &hostname) == 0);
				verify(nvlist_lookup_uint64(config,
				    ZPOOL_CONFIG_TIMESTAMP, &timestamp) == 0);
				t = timestamp;
				(void) fprintf(stderr, gettext("cannot import "
				    "'%s': pool may be in use from other "
				    "system, it was last accessed by %s "
				    "(hostid: 0x%lx) on %s"), name, hostname,
				    (unsigned long)hostid,
				    asctime(localtime(&t)));
				(void) fprintf(stderr, gettext("use '-f' to "
				    "import anyway\n"));
				return (1);
			}
		} else {
			(void) fprintf(stderr, gettext("cannot import '%s': "
			    "pool may be in use from other system\n"), name);
			(void) fprintf(stderr, gettext("use '-f' to import "
			    "anyway\n"));
			return (1);
		}
	}

	if (zpool_import_props(g_zfs, config, newname, props, flags) != 0)
		return (1);

	if (newname != NULL)
		name = (char *)newname;

	if ((zhp = zpool_open_canfail(g_zfs, name)) == NULL)
		return (1);

	if (zpool_get_state(zhp) != POOL_STATE_UNAVAIL &&
	    !(flags & ZFS_IMPORT_ONLY) &&
	    zpool_enable_datasets(zhp, mntopts, 0) != 0) {
		zpool_close(zhp);
		return (1);
	}

	zpool_close(zhp);
	return (0);
}

/*
 * zpool import [-d dir] [-D]
 *       import [-o mntopts] [-o prop=value] ... [-R root] [-D]
 *              [-d dir | -c cachefile] [-f] -a
 *       import [-o mntopts] [-o prop=value] ... [-R root] [-D]
 *              [-d dir | -c cachefile] [-f] [-n] [-F] <pool | id> [newpool]
 *
 *	 -c	Read pool information from a cachefile instead of searching
 *		devices.
 *
 *       -d	Scan in a specific directory, other than /dev/dsk.  More than
 *		one directory can be specified using multiple '-d' options.
 *
 *       -D     Scan for previously destroyed pools or import all or only
 *              specified destroyed pools.
 *
 *       -R	Temporarily import the pool, with all mountpoints relative to
 *		the given root.  The pool will remain exported when the machine
 *		is rebooted.
 *
 *       -V	Import even in the presence of faulted vdevs.  This is an
 *       	intentionally undocumented option for testing purposes, and
 *       	treats the pool configuration as complete, leaving any bad
 *		vdevs in the FAULTED state. In other words, it does verbatim
 *		import.
 *
 *       -f	Force import, even if it appears that the pool is active.
 *
 *       -F     Attempt rewind if necessary.
 *
 *       -n     See if rewind would work, but don't actually rewind.
 *
 *       -N     Import the pool but don't mount datasets.
 *
 *       -T     Specify a starting txg to use for import. This option is
 *       	intentionally undocumented option for testing purposes.
 *
 *       -a	Import all pools found.
 *
 *       -o	Set property=value and/or temporary mount options (without '=').
 *
 * The import command scans for pools to import, and import pools based on pool
 * name and GUID.  The pool can also be renamed as part of the import process.
 */
int
zpool_do_import(int argc, char **argv)
{
	char **searchdirs = NULL;
	int nsearch = 0;
	int c;
	int err = 0;
	nvlist_t *pools = NULL;
	boolean_t do_all = B_FALSE;
	boolean_t do_destroyed = B_FALSE;
	char *mntopts = NULL;
	nvpair_t *elem;
	nvlist_t *config;
	uint64_t searchguid = 0;
	char *searchname = NULL;
	char *propval;
	nvlist_t *found_config;
	nvlist_t *policy = NULL;
	nvlist_t *props = NULL;
	boolean_t first;
	int flags = ZFS_IMPORT_NORMAL;
	uint32_t rewind_policy = ZPOOL_NO_REWIND;
	boolean_t dryrun = B_FALSE;
	boolean_t do_rewind = B_FALSE;
	boolean_t xtreme_rewind = B_FALSE;
	uint64_t pool_state, txg = -1ULL;
	char *cachefile = NULL;
	importargs_t idata = { 0 };
	char *endptr;

	/* check options */
	while ((c = getopt(argc, argv, ":aCc:d:DEfFmnNo:rR:T:VX")) != -1) {
		switch (c) {
		case 'a':
			do_all = B_TRUE;
			break;
		case 'c':
			cachefile = optarg;
			break;
		case 'd':
			if (searchdirs == NULL) {
				searchdirs = safe_malloc(sizeof (char *));
			} else {
				char **tmp = safe_malloc((nsearch + 1) *
				    sizeof (char *));
				bcopy(searchdirs, tmp, nsearch *
				    sizeof (char *));
				free(searchdirs);
				searchdirs = tmp;
			}
			searchdirs[nsearch++] = optarg;
			break;
		case 'D':
			do_destroyed = B_TRUE;
			break;
		case 'f':
			flags |= ZFS_IMPORT_ANY_HOST;
			break;
		case 'F':
			do_rewind = B_TRUE;
			break;
		case 'm':
			flags |= ZFS_IMPORT_MISSING_LOG;
			break;
		case 'n':
			dryrun = B_TRUE;
			break;
		case 'N':
			flags |= ZFS_IMPORT_ONLY;
			break;
		case 'o':
			if ((propval = strchr(optarg, '=')) != NULL) {
				*propval = '\0';
				propval++;
				if (add_prop_list(optarg, propval,
				    &props, B_TRUE))
					goto error;
			} else {
				mntopts = optarg;
			}
			break;
		case 'R':
			if (add_prop_list(zpool_prop_to_name(
			    ZPOOL_PROP_ALTROOT), optarg, &props, B_TRUE))
				goto error;
			if (nvlist_lookup_string(props,
			    zpool_prop_to_name(ZPOOL_PROP_CACHEFILE),
			    &propval) == 0)
				break;
			if (add_prop_list(zpool_prop_to_name(
			    ZPOOL_PROP_CACHEFILE), "none", &props, B_TRUE))
				goto error;
			break;
		case 'T':
			errno = 0;
			txg = strtoull(optarg, &endptr, 10);
			if (errno != 0 || *endptr != '\0') {
				(void) fprintf(stderr,
				    gettext("invalid txg value\n"));
				usage(B_FALSE);
			}
			rewind_policy = ZPOOL_DO_REWIND | ZPOOL_EXTREME_REWIND;
			break;
		case 'V':
			flags |= ZFS_IMPORT_VERBATIM;
			break;
		case 'X':
			xtreme_rewind = B_TRUE;
			break;
		case ':':
			(void) fprintf(stderr, gettext("missing argument for "
			    "'%c' option\n"), optopt);
			usage(B_FALSE);
			break;
		case '?':
			(void) fprintf(stderr, gettext("invalid option '%c'\n"),
			    optopt);
			usage(B_FALSE);
		}
	}

	argc -= optind;
	argv += optind;

	if (cachefile && nsearch != 0) {
		(void) fprintf(stderr, gettext("-c is incompatible with -d\n"));
		usage(B_FALSE);
	}

	if ((dryrun || xtreme_rewind) && !do_rewind) {
		(void) fprintf(stderr,
		    gettext("-n or -X only meaningful with -F\n"));
		usage(B_FALSE);
	}
	if (dryrun)
		rewind_policy = ZPOOL_TRY_REWIND;
	else if (do_rewind)
		rewind_policy = ZPOOL_DO_REWIND;
	if (xtreme_rewind)
		rewind_policy |= ZPOOL_EXTREME_REWIND;

	/* In the future, we can capture further policy and include it here */
	if (nvlist_alloc(&policy, NV_UNIQUE_NAME, 0) != 0 ||
	    nvlist_add_uint64(policy, ZPOOL_REWIND_REQUEST_TXG, txg) != 0 ||
	    nvlist_add_uint32(policy, ZPOOL_REWIND_REQUEST, rewind_policy) != 0)
		goto error;

	if (searchdirs == NULL) {
		searchdirs = safe_malloc(sizeof (char *));
		searchdirs[0] = "/dev/dsk";
		nsearch = 1;
	}

	/* check argument count */
	if (do_all) {
		if (argc != 0) {
			(void) fprintf(stderr, gettext("too many arguments\n"));
			usage(B_FALSE);
		}
	} else {
		if (argc > 2) {
			(void) fprintf(stderr, gettext("too many arguments\n"));
			usage(B_FALSE);
		}

		/*
		 * Check for the SYS_CONFIG privilege.  We do this explicitly
		 * here because otherwise any attempt to discover pools will
		 * silently fail.
		 */
		if (argc == 0 && !priv_ineffect(PRIV_SYS_CONFIG)) {
			(void) fprintf(stderr, gettext("cannot "
			    "discover pools: permission denied\n"));
			free(searchdirs);
			nvlist_free(policy);
			return (1);
		}
	}

	/*
	 * Depending on the arguments given, we do one of the following:
	 *
	 *	<none>	Iterate through all pools and display information about
	 *		each one.
	 *
	 *	-a	Iterate through all pools and try to import each one.
	 *
	 *	<id>	Find the pool that corresponds to the given GUID/pool
	 *		name and import that one.
	 *
	 *	-D	Above options applies only to destroyed pools.
	 */
	if (argc != 0) {
		char *endptr;

		errno = 0;
		searchguid = strtoull(argv[0], &endptr, 10);
		if (errno != 0 || *endptr != '\0')
			searchname = argv[0];
		found_config = NULL;

		/*
		 * User specified a name or guid.  Ensure it's unique.
		 */
		idata.unique = B_TRUE;
	}


	idata.path = searchdirs;
	idata.paths = nsearch;
	idata.poolname = searchname;
	idata.guid = searchguid;
	idata.cachefile = cachefile;

	pools = zpool_search_import(g_zfs, &idata);

	if (pools != NULL && idata.exists &&
	    (argc == 1 || strcmp(argv[0], argv[1]) == 0)) {
		(void) fprintf(stderr, gettext("cannot import '%s': "
		    "a pool with that name already exists\n"),
		    argv[0]);
		(void) fprintf(stderr, gettext("use the form '%s "
		    "<pool | id> <newpool>' to give it a new name\n"),
		    "zpool import");
		err = 1;
	} else if (pools == NULL && idata.exists) {
		(void) fprintf(stderr, gettext("cannot import '%s': "
		    "a pool with that name is already created/imported,\n"),
		    argv[0]);
		(void) fprintf(stderr, gettext("and no additional pools "
		    "with that name were found\n"));
		err = 1;
	} else if (pools == NULL) {
		if (argc != 0) {
			(void) fprintf(stderr, gettext("cannot import '%s': "
			    "no such pool available\n"), argv[0]);
		}
		err = 1;
	}

	if (err == 1) {
		free(searchdirs);
		nvlist_free(policy);
		return (1);
	}

	/*
	 * At this point we have a list of import candidate configs. Even if
	 * we were searching by pool name or guid, we still need to
	 * post-process the list to deal with pool state and possible
	 * duplicate names.
	 */
	err = 0;
	elem = NULL;
	first = B_TRUE;
	while ((elem = nvlist_next_nvpair(pools, elem)) != NULL) {

		verify(nvpair_value_nvlist(elem, &config) == 0);

		verify(nvlist_lookup_uint64(config, ZPOOL_CONFIG_POOL_STATE,
		    &pool_state) == 0);
		if (!do_destroyed && pool_state == POOL_STATE_DESTROYED)
			continue;
		if (do_destroyed && pool_state != POOL_STATE_DESTROYED)
			continue;

		verify(nvlist_add_nvlist(config, ZPOOL_REWIND_POLICY,
		    policy) == 0);

		if (argc == 0) {
			if (first)
				first = B_FALSE;
			else if (!do_all)
				(void) printf("\n");

			if (do_all) {
				err |= do_import(config, NULL, mntopts,
				    props, flags);
			} else {
				show_import(config);
			}
		} else if (searchname != NULL) {
			char *name;

			/*
			 * We are searching for a pool based on name.
			 */
			verify(nvlist_lookup_string(config,
			    ZPOOL_CONFIG_POOL_NAME, &name) == 0);

			if (strcmp(name, searchname) == 0) {
				if (found_config != NULL) {
					(void) fprintf(stderr, gettext(
					    "cannot import '%s': more than "
					    "one matching pool\n"), searchname);
					(void) fprintf(stderr, gettext(
					    "import by numeric ID instead\n"));
					err = B_TRUE;
				}
				found_config = config;
			}
		} else {
			uint64_t guid;

			/*
			 * Search for a pool by guid.
			 */
			verify(nvlist_lookup_uint64(config,
			    ZPOOL_CONFIG_POOL_GUID, &guid) == 0);

			if (guid == searchguid)
				found_config = config;
		}
	}

	/*
	 * If we were searching for a specific pool, verify that we found a
	 * pool, and then do the import.
	 */
	if (argc != 0 && err == 0) {
		if (found_config == NULL) {
			(void) fprintf(stderr, gettext("cannot import '%s': "
			    "no such pool available\n"), argv[0]);
			err = B_TRUE;
		} else {
			err |= do_import(found_config, argc == 1 ? NULL :
			    argv[1], mntopts, props, flags);
		}
	}

	/*
	 * If we were just looking for pools, report an error if none were
	 * found.
	 */
	if (argc == 0 && first)
		(void) fprintf(stderr,
		    gettext("no pools available to import\n"));

error:
	nvlist_free(props);
	nvlist_free(pools);
	nvlist_free(policy);
	free(searchdirs);

	return (err ? 1 : 0);
}

typedef struct iostat_cbdata {
	zpool_list_t *cb_list;
	int cb_verbose;
	int cb_iteration;
	int cb_namewidth;
} iostat_cbdata_t;

static void
print_iostat_separator(iostat_cbdata_t *cb)
{
	int i = 0;

	for (i = 0; i < cb->cb_namewidth; i++)
		(void) printf("-");
	(void) printf("  -----  -----  -----  -----  -----  -----\n");
}

static void
print_iostat_header(iostat_cbdata_t *cb)
{
	(void) printf("%*s     capacity     operations    bandwidth\n",
	    cb->cb_namewidth, "");
	(void) printf("%-*s  alloc   free   read  write   read  write\n",
	    cb->cb_namewidth, "pool");
	print_iostat_separator(cb);
}

/*
 * Display a single statistic.
 */
static void
print_one_stat(uint64_t value)
{
	char buf[64];

	zfs_nicenum(value, buf, sizeof (buf));
	(void) printf("  %5s", buf);
}

/*
 * Print out all the statistics for the given vdev.  This can either be the
 * toplevel configuration, or called recursively.  If 'name' is NULL, then this
 * is a verbose output, and we don't want to display the toplevel pool stats.
 */
void
print_vdev_stats(zpool_handle_t *zhp, const char *name, nvlist_t *oldnv,
    nvlist_t *newnv, iostat_cbdata_t *cb, int depth)
{
	nvlist_t **oldchild, **newchild;
	uint_t c, children;
	vdev_stat_t *oldvs, *newvs;
	vdev_stat_t zerovs = { 0 };
	uint64_t tdelta;
	double scale;
	char *vname;

	if (oldnv != NULL) {
		verify(nvlist_lookup_uint64_array(oldnv,
		    ZPOOL_CONFIG_VDEV_STATS, (uint64_t **)&oldvs, &c) == 0);
	} else {
		oldvs = &zerovs;
	}

	verify(nvlist_lookup_uint64_array(newnv, ZPOOL_CONFIG_VDEV_STATS,
	    (uint64_t **)&newvs, &c) == 0);

	if (strlen(name) + depth > cb->cb_namewidth)
		(void) printf("%*s%s", depth, "", name);
	else
		(void) printf("%*s%s%*s", depth, "", name,
		    (int)(cb->cb_namewidth - strlen(name) - depth), "");

	tdelta = newvs->vs_timestamp - oldvs->vs_timestamp;

	if (tdelta == 0)
		scale = 1.0;
	else
		scale = (double)NANOSEC / tdelta;

	/* only toplevel vdevs have capacity stats */
	if (newvs->vs_space == 0) {
		(void) printf("      -      -");
	} else {
		print_one_stat(newvs->vs_alloc);
		print_one_stat(newvs->vs_space - newvs->vs_alloc);
	}

	print_one_stat((uint64_t)(scale * (newvs->vs_ops[ZIO_TYPE_READ] -
	    oldvs->vs_ops[ZIO_TYPE_READ])));

	print_one_stat((uint64_t)(scale * (newvs->vs_ops[ZIO_TYPE_WRITE] -
	    oldvs->vs_ops[ZIO_TYPE_WRITE])));

	print_one_stat((uint64_t)(scale * (newvs->vs_bytes[ZIO_TYPE_READ] -
	    oldvs->vs_bytes[ZIO_TYPE_READ])));

	print_one_stat((uint64_t)(scale * (newvs->vs_bytes[ZIO_TYPE_WRITE] -
	    oldvs->vs_bytes[ZIO_TYPE_WRITE])));

	(void) printf("\n");

	if (!cb->cb_verbose)
		return;

	if (nvlist_lookup_nvlist_array(newnv, ZPOOL_CONFIG_CHILDREN,
	    &newchild, &children) != 0)
		return;

	if (oldnv && nvlist_lookup_nvlist_array(oldnv, ZPOOL_CONFIG_CHILDREN,
	    &oldchild, &c) != 0)
		return;

	for (c = 0; c < children; c++) {
		uint64_t ishole = B_FALSE;

		if (nvlist_lookup_uint64(newchild[c],
		    ZPOOL_CONFIG_IS_HOLE, &ishole) == 0 && ishole)
			continue;

		vname = zpool_vdev_name(g_zfs, zhp, newchild[c], B_FALSE);
		print_vdev_stats(zhp, vname, oldnv ? oldchild[c] : NULL,
		    newchild[c], cb, depth + 2);
		free(vname);
	}

	/*
	 * Include level 2 ARC devices in iostat output
	 */
	if (nvlist_lookup_nvlist_array(newnv, ZPOOL_CONFIG_L2CACHE,
	    &newchild, &children) != 0)
		return;

	if (oldnv && nvlist_lookup_nvlist_array(oldnv, ZPOOL_CONFIG_L2CACHE,
	    &oldchild, &c) != 0)
		return;

	if (children > 0) {
		(void) printf("%-*s      -      -      -      -      -      "
		    "-\n", cb->cb_namewidth, "cache");
		for (c = 0; c < children; c++) {
			vname = zpool_vdev_name(g_zfs, zhp, newchild[c],
			    B_FALSE);
			print_vdev_stats(zhp, vname, oldnv ? oldchild[c] : NULL,
			    newchild[c], cb, depth + 2);
			free(vname);
		}
	}
}

static int
refresh_iostat(zpool_handle_t *zhp, void *data)
{
	iostat_cbdata_t *cb = data;
	boolean_t missing;

	/*
	 * If the pool has disappeared, remove it from the list and continue.
	 */
	if (zpool_refresh_stats(zhp, &missing) != 0)
		return (-1);

	if (missing)
		pool_list_remove(cb->cb_list, zhp);

	return (0);
}

/*
 * Callback to print out the iostats for the given pool.
 */
int
print_iostat(zpool_handle_t *zhp, void *data)
{
	iostat_cbdata_t *cb = data;
	nvlist_t *oldconfig, *newconfig;
	nvlist_t *oldnvroot, *newnvroot;

	newconfig = zpool_get_config(zhp, &oldconfig);

	if (cb->cb_iteration == 1)
		oldconfig = NULL;

	verify(nvlist_lookup_nvlist(newconfig, ZPOOL_CONFIG_VDEV_TREE,
	    &newnvroot) == 0);

	if (oldconfig == NULL)
		oldnvroot = NULL;
	else
		verify(nvlist_lookup_nvlist(oldconfig, ZPOOL_CONFIG_VDEV_TREE,
		    &oldnvroot) == 0);

	/*
	 * Print out the statistics for the pool.
	 */
	print_vdev_stats(zhp, zpool_get_name(zhp), oldnvroot, newnvroot, cb, 0);

	if (cb->cb_verbose)
		print_iostat_separator(cb);

	return (0);
}

int
get_namewidth(zpool_handle_t *zhp, void *data)
{
	iostat_cbdata_t *cb = data;
	nvlist_t *config, *nvroot;

	if ((config = zpool_get_config(zhp, NULL)) != NULL) {
		verify(nvlist_lookup_nvlist(config, ZPOOL_CONFIG_VDEV_TREE,
		    &nvroot) == 0);
		if (!cb->cb_verbose)
			cb->cb_namewidth = strlen(zpool_get_name(zhp));
		else
			cb->cb_namewidth = max_width(zhp, nvroot, 0, 0);
	}

	/*
	 * The width must fall into the range [10,38].  The upper limit is the
	 * maximum we can have and still fit in 80 columns.
	 */
	if (cb->cb_namewidth < 10)
		cb->cb_namewidth = 10;
	if (cb->cb_namewidth > 38)
		cb->cb_namewidth = 38;

	return (0);
}

/*
 * Parse the input string, get the 'interval' and 'count' value if there is one.
 */
static void
get_interval_count(int *argcp, char **argv, unsigned long *iv,
    unsigned long *cnt)
{
	unsigned long interval = 0, count = 0;
	int argc = *argcp, errno;

	/*
	 * Determine if the last argument is an integer or a pool name
	 */
	if (argc > 0 && isdigit(argv[argc - 1][0])) {
		char *end;

		errno = 0;
		interval = strtoul(argv[argc - 1], &end, 10);

		if (*end == '\0' && errno == 0) {
			if (interval == 0) {
				(void) fprintf(stderr, gettext("interval "
				    "cannot be zero\n"));
				usage(B_FALSE);
			}
			/*
			 * Ignore the last parameter
			 */
			argc--;
		} else {
			/*
			 * If this is not a valid number, just plow on.  The
			 * user will get a more informative error message later
			 * on.
			 */
			interval = 0;
		}
	}

	/*
	 * If the last argument is also an integer, then we have both a count
	 * and an interval.
	 */
	if (argc > 0 && isdigit(argv[argc - 1][0])) {
		char *end;

		errno = 0;
		count = interval;
		interval = strtoul(argv[argc - 1], &end, 10);

		if (*end == '\0' && errno == 0) {
			if (interval == 0) {
				(void) fprintf(stderr, gettext("interval "
				    "cannot be zero\n"));
				usage(B_FALSE);
			}

			/*
			 * Ignore the last parameter
			 */
			argc--;
		} else {
			interval = 0;
		}
	}

	*iv = interval;
	*cnt = count;
	*argcp = argc;
}

static void
get_timestamp_arg(char c)
{
	if (c == 'u')
		timestamp_fmt = UDATE;
	else if (c == 'd')
		timestamp_fmt = DDATE;
	else
		usage(B_FALSE);
}

/*
 * zpool iostat [-v] [-T d|u] [pool] ... [interval [count]]
 *
 *	-v	Display statistics for individual vdevs
 *	-T	Display a timestamp in date(1) or Unix format
 *
 * This command can be tricky because we want to be able to deal with pool
 * creation/destruction as well as vdev configuration changes.  The bulk of this
 * processing is handled by the pool_list_* routines in zpool_iter.c.  We rely
 * on pool_list_update() to detect the addition of new pools.  Configuration
 * changes are all handled within libzfs.
 */
int
zpool_do_iostat(int argc, char **argv)
{
	int c;
	int ret;
	int npools;
	unsigned long interval = 0, count = 0;
	zpool_list_t *list;
	boolean_t verbose = B_FALSE;
	iostat_cbdata_t cb;

	/* check options */
	while ((c = getopt(argc, argv, "T:v")) != -1) {
		switch (c) {
		case 'T':
			get_timestamp_arg(*optarg);
			break;
		case 'v':
			verbose = B_TRUE;
			break;
		case '?':
			(void) fprintf(stderr, gettext("invalid option '%c'\n"),
			    optopt);
			usage(B_FALSE);
		}
	}

	argc -= optind;
	argv += optind;

	get_interval_count(&argc, argv, &interval, &count);

	/*
	 * Construct the list of all interesting pools.
	 */
	ret = 0;
	if ((list = pool_list_get(argc, argv, NULL, &ret)) == NULL)
		return (1);

	if (pool_list_count(list) == 0 && argc != 0) {
		pool_list_free(list);
		return (1);
	}

	if (pool_list_count(list) == 0 && interval == 0) {
		pool_list_free(list);
		(void) fprintf(stderr, gettext("no pools available\n"));
		return (1);
	}

	/*
	 * Enter the main iostat loop.
	 */
	cb.cb_list = list;
	cb.cb_verbose = verbose;
	cb.cb_iteration = 0;
	cb.cb_namewidth = 0;

	for (;;) {
		pool_list_update(list);

		if ((npools = pool_list_count(list)) == 0)
			break;

		/*
		 * Refresh all statistics.  This is done as an explicit step
		 * before calculating the maximum name width, so that any
		 * configuration changes are properly accounted for.
		 */
		(void) pool_list_iter(list, B_FALSE, refresh_iostat, &cb);

		/*
		 * Iterate over all pools to determine the maximum width
		 * for the pool / device name column across all pools.
		 */
		cb.cb_namewidth = 0;
		(void) pool_list_iter(list, B_FALSE, get_namewidth, &cb);

		if (timestamp_fmt != NODATE)
			print_timestamp(timestamp_fmt);

		/*
		 * If it's the first time, or verbose mode, print the header.
		 */
		if (++cb.cb_iteration == 1 || verbose)
			print_iostat_header(&cb);

		(void) pool_list_iter(list, B_FALSE, print_iostat, &cb);

		/*
		 * If there's more than one pool, and we're not in verbose mode
		 * (which prints a separator for us), then print a separator.
		 */
		if (npools > 1 && !verbose)
			print_iostat_separator(&cb);

		if (verbose)
			(void) printf("\n");

		/*
		 * Flush the output so that redirection to a file isn't buffered
		 * indefinitely.
		 */
		(void) fflush(stdout);

		if (interval == 0)
			break;

		if (count != 0 && --count == 0)
			break;

		(void) sleep(interval);
	}

	pool_list_free(list);

	return (ret);
}

typedef struct list_cbdata {
	boolean_t	cb_scripted;
	boolean_t	cb_first;
	zprop_list_t	*cb_proplist;
} list_cbdata_t;

/*
 * Given a list of columns to display, output appropriate headers for each one.
 */
static void
print_header(zprop_list_t *pl)
{
	const char *header;
	boolean_t first = B_TRUE;
	boolean_t right_justify;

	for (; pl != NULL; pl = pl->pl_next) {
		if (pl->pl_prop == ZPROP_INVAL)
			continue;

		if (!first)
			(void) printf("  ");
		else
			first = B_FALSE;

		header = zpool_prop_column_name(pl->pl_prop);
		right_justify = zpool_prop_align_right(pl->pl_prop);

		if (pl->pl_next == NULL && !right_justify)
			(void) printf("%s", header);
		else if (right_justify)
			(void) printf("%*s", pl->pl_width, header);
		else
			(void) printf("%-*s", pl->pl_width, header);
	}

	(void) printf("\n");
}

/*
 * Given a pool and a list of properties, print out all the properties according
 * to the described layout.
 */
static void
print_pool(zpool_handle_t *zhp, zprop_list_t *pl, int scripted)
{
	boolean_t first = B_TRUE;
	char property[ZPOOL_MAXPROPLEN];
	char *propstr;
	boolean_t right_justify;
	int width;

	for (; pl != NULL; pl = pl->pl_next) {
		if (!first) {
			if (scripted)
				(void) printf("\t");
			else
				(void) printf("  ");
		} else {
			first = B_FALSE;
		}

		right_justify = B_FALSE;
		if (pl->pl_prop != ZPROP_INVAL) {
			if (zpool_get_prop(zhp, pl->pl_prop, property,
			    sizeof (property), NULL) != 0)
				propstr = "-";
			else
				propstr = property;

			right_justify = zpool_prop_align_right(pl->pl_prop);
		} else {
			propstr = "-";
		}

		width = pl->pl_width;

		/*
		 * If this is being called in scripted mode, or if this is the
		 * last column and it is left-justified, don't include a width
		 * format specifier.
		 */
		if (scripted || (pl->pl_next == NULL && !right_justify))
			(void) printf("%s", propstr);
		else if (right_justify)
			(void) printf("%*s", width, propstr);
		else
			(void) printf("%-*s", width, propstr);
	}

	(void) printf("\n");
}

/*
 * Generic callback function to list a pool.
 */
int
list_callback(zpool_handle_t *zhp, void *data)
{
	list_cbdata_t *cbp = data;

	if (cbp->cb_first) {
		if (!cbp->cb_scripted)
			print_header(cbp->cb_proplist);
		cbp->cb_first = B_FALSE;
	}

	print_pool(zhp, cbp->cb_proplist, cbp->cb_scripted);

	return (0);
}

/*
 * zpool list [-H] [-o prop[,prop]*] [-T d|u] [pool] ... [interval [count]]
 *
 *	-H	Scripted mode.  Don't display headers, and separate properties
 *		by a single tab.
 *	-o	List of properties to display.  Defaults to
 *		"name,size,allocated,free,capacity,health,altroot"
 *	-T	Display a timestamp in date(1) or Unix format
 *
 * List all pools in the system, whether or not they're healthy.  Output space
 * statistics for each one, as well as health status summary.
 */
int
zpool_do_list(int argc, char **argv)
{
	int c;
	int ret;
	list_cbdata_t cb = { 0 };
	static char default_props[] =
	    "name,size,allocated,free,capacity,dedupratio,health,altroot";
	char *props = default_props;
	unsigned long interval = 0, count = 0;

	/* check options */
	while ((c = getopt(argc, argv, ":Ho:T:")) != -1) {
		switch (c) {
		case 'H':
			cb.cb_scripted = B_TRUE;
			break;
		case 'o':
			props = optarg;
			break;
		case 'T':
			get_timestamp_arg(*optarg);
			break;
		case ':':
			(void) fprintf(stderr, gettext("missing argument for "
			    "'%c' option\n"), optopt);
			usage(B_FALSE);
			break;
		case '?':
			(void) fprintf(stderr, gettext("invalid option '%c'\n"),
			    optopt);
			usage(B_FALSE);
		}
	}

	argc -= optind;
	argv += optind;

	get_interval_count(&argc, argv, &interval, &count);

	if (zprop_get_list(g_zfs, props, &cb.cb_proplist, ZFS_TYPE_POOL) != 0)
		usage(B_FALSE);

	cb.cb_first = B_TRUE;

	for (;;) {

		if (timestamp_fmt != NODATE)
			print_timestamp(timestamp_fmt);

		ret = for_each_pool(argc, argv, B_TRUE, &cb.cb_proplist,
		    list_callback, &cb);

		if (argc == 0 && cb.cb_first && !cb.cb_scripted) {
			(void) printf(gettext("no pools available\n"));
			zprop_free_list(cb.cb_proplist);
			return (0);
		}

		if (interval == 0)
			break;

		if (count != 0 && --count == 0)
			break;

		(void) sleep(interval);
	}

	zprop_free_list(cb.cb_proplist);
	return (ret);
}

static nvlist_t *
zpool_get_vdev_by_name(nvlist_t *nv, char *name)
{
	nvlist_t **child;
	uint_t c, children;
	nvlist_t *match;
	char *path;

	if (nvlist_lookup_nvlist_array(nv, ZPOOL_CONFIG_CHILDREN,
	    &child, &children) != 0) {
		verify(nvlist_lookup_string(nv, ZPOOL_CONFIG_PATH, &path) == 0);
		if (strncmp(name, "/dev/dsk/", 9) == 0)
			name += 9;
		if (strncmp(path, "/dev/dsk/", 9) == 0)
			path += 9;
		if (strcmp(name, path) == 0)
			return (nv);
		return (NULL);
	}

	for (c = 0; c < children; c++)
		if ((match = zpool_get_vdev_by_name(child[c], name)) != NULL)
			return (match);

	return (NULL);
}

static int
zpool_do_attach_or_replace(int argc, char **argv, int replacing)
{
	boolean_t force = B_FALSE;
	int c;
	nvlist_t *nvroot;
	char *poolname, *old_disk, *new_disk;
	zpool_handle_t *zhp;
	int ret;

	/* check options */
	while ((c = getopt(argc, argv, "f")) != -1) {
		switch (c) {
		case 'f':
			force = B_TRUE;
			break;
		case '?':
			(void) fprintf(stderr, gettext("invalid option '%c'\n"),
			    optopt);
			usage(B_FALSE);
		}
	}

	argc -= optind;
	argv += optind;

	/* get pool name and check number of arguments */
	if (argc < 1) {
		(void) fprintf(stderr, gettext("missing pool name argument\n"));
		usage(B_FALSE);
	}

	poolname = argv[0];

	if (argc < 2) {
		(void) fprintf(stderr,
		    gettext("missing <device> specification\n"));
		usage(B_FALSE);
	}

	old_disk = argv[1];

	if (argc < 3) {
		if (!replacing) {
			(void) fprintf(stderr,
			    gettext("missing <new_device> specification\n"));
			usage(B_FALSE);
		}
		new_disk = old_disk;
		argc -= 1;
		argv += 1;
	} else {
		new_disk = argv[2];
		argc -= 2;
		argv += 2;
	}

	if (argc > 1) {
		(void) fprintf(stderr, gettext("too many arguments\n"));
		usage(B_FALSE);
	}

	if ((zhp = zpool_open(g_zfs, poolname)) == NULL)
		return (1);

	if (zpool_get_config(zhp, NULL) == NULL) {
		(void) fprintf(stderr, gettext("pool '%s' is unavailable\n"),
		    poolname);
		zpool_close(zhp);
		return (1);
	}

	nvroot = make_root_vdev(zhp, force, B_FALSE, replacing, B_FALSE,
	    argc, argv);
	if (nvroot == NULL) {
		zpool_close(zhp);
		return (1);
	}

	ret = zpool_vdev_attach(zhp, old_disk, new_disk, nvroot, replacing);

	nvlist_free(nvroot);
	zpool_close(zhp);

	return (ret);
}

/*
 * zpool replace [-f] <pool> <device> <new_device>
 *
 *	-f	Force attach, even if <new_device> appears to be in use.
 *
 * Replace <device> with <new_device>.
 */
/* ARGSUSED */
int
zpool_do_replace(int argc, char **argv)
{
	return (zpool_do_attach_or_replace(argc, argv, B_TRUE));
}

/*
 * zpool attach [-f] <pool> <device> <new_device>
 *
 *	-f	Force attach, even if <new_device> appears to be in use.
 *
 * Attach <new_device> to the mirror containing <device>.  If <device> is not
 * part of a mirror, then <device> will be transformed into a mirror of
 * <device> and <new_device>.  In either case, <new_device> will begin life
 * with a DTL of [0, now], and will immediately begin to resilver itself.
 */
int
zpool_do_attach(int argc, char **argv)
{
	return (zpool_do_attach_or_replace(argc, argv, B_FALSE));
}

/*
 * zpool detach [-f] <pool> <device>
 *
 *	-f	Force detach of <device>, even if DTLs argue against it
 *		(not supported yet)
 *
 * Detach a device from a mirror.  The operation will be refused if <device>
 * is the last device in the mirror, or if the DTLs indicate that this device
 * has the only valid copy of some data.
 */
/* ARGSUSED */
int
zpool_do_detach(int argc, char **argv)
{
	int c;
	char *poolname, *path;
	zpool_handle_t *zhp;
	int ret;

	/* check options */
	while ((c = getopt(argc, argv, "f")) != -1) {
		switch (c) {
		case 'f':
		case '?':
			(void) fprintf(stderr, gettext("invalid option '%c'\n"),
			    optopt);
			usage(B_FALSE);
		}
	}

	argc -= optind;
	argv += optind;

	/* get pool name and check number of arguments */
	if (argc < 1) {
		(void) fprintf(stderr, gettext("missing pool name argument\n"));
		usage(B_FALSE);
	}

	if (argc < 2) {
		(void) fprintf(stderr,
		    gettext("missing <device> specification\n"));
		usage(B_FALSE);
	}

	poolname = argv[0];
	path = argv[1];

	if ((zhp = zpool_open(g_zfs, poolname)) == NULL)
		return (1);

	ret = zpool_vdev_detach(zhp, path);

	zpool_close(zhp);

	return (ret);
}

/*
 * zpool split [-n] [-o prop=val] ...
 *		[-o mntopt] ...
 *		[-R altroot] <pool> <newpool> [<device> ...]
 *
 *	-n	Do not split the pool, but display the resulting layout if
 *		it were to be split.
 *	-o	Set property=value, or set mount options.
 *	-R	Mount the split-off pool under an alternate root.
 *
 * Splits the named pool and gives it the new pool name.  Devices to be split
 * off may be listed, provided that no more than one device is specified
 * per top-level vdev mirror.  The newly split pool is left in an exported
 * state unless -R is specified.
 *
 * Restrictions: the top-level of the pool pool must only be made up of
 * mirrors; all devices in the pool must be healthy; no device may be
 * undergoing a resilvering operation.
 */
int
zpool_do_split(int argc, char **argv)
{
	char *srcpool, *newpool, *propval;
	char *mntopts = NULL;
	splitflags_t flags;
	int c, ret = 0;
	zpool_handle_t *zhp;
	nvlist_t *config, *props = NULL;

	flags.dryrun = B_FALSE;
	flags.import = B_FALSE;

	/* check options */
	while ((c = getopt(argc, argv, ":R:no:")) != -1) {
		switch (c) {
		case 'R':
			flags.import = B_TRUE;
			if (add_prop_list(
			    zpool_prop_to_name(ZPOOL_PROP_ALTROOT), optarg,
			    &props, B_TRUE) != 0) {
				if (props)
					nvlist_free(props);
				usage(B_FALSE);
			}
			break;
		case 'n':
			flags.dryrun = B_TRUE;
			break;
		case 'o':
			if ((propval = strchr(optarg, '=')) != NULL) {
				*propval = '\0';
				propval++;
				if (add_prop_list(optarg, propval,
				    &props, B_TRUE) != 0) {
					if (props)
						nvlist_free(props);
					usage(B_FALSE);
				}
			} else {
				mntopts = optarg;
			}
			break;
		case ':':
			(void) fprintf(stderr, gettext("missing argument for "
			    "'%c' option\n"), optopt);
			usage(B_FALSE);
			break;
		case '?':
			(void) fprintf(stderr, gettext("invalid option '%c'\n"),
			    optopt);
			usage(B_FALSE);
			break;
		}
	}

	if (!flags.import && mntopts != NULL) {
		(void) fprintf(stderr, gettext("setting mntopts is only "
		    "valid when importing the pool\n"));
		usage(B_FALSE);
	}

	argc -= optind;
	argv += optind;

	if (argc < 1) {
		(void) fprintf(stderr, gettext("Missing pool name\n"));
		usage(B_FALSE);
	}
	if (argc < 2) {
		(void) fprintf(stderr, gettext("Missing new pool name\n"));
		usage(B_FALSE);
	}

	srcpool = argv[0];
	newpool = argv[1];

	argc -= 2;
	argv += 2;

	if ((zhp = zpool_open(g_zfs, srcpool)) == NULL)
		return (1);

	config = split_mirror_vdev(zhp, newpool, props, flags, argc, argv);
	if (config == NULL) {
		ret = 1;
	} else {
		if (flags.dryrun) {
			(void) printf(gettext("would create '%s' with the "
			    "following layout:\n\n"), newpool);
			print_vdev_tree(NULL, newpool, config, 0, B_FALSE);
		}
		nvlist_free(config);
	}

	zpool_close(zhp);

	if (ret != 0 || flags.dryrun || !flags.import)
		return (ret);

	/*
	 * The split was successful. Now we need to open the new
	 * pool and import it.
	 */
	if ((zhp = zpool_open_canfail(g_zfs, newpool)) == NULL)
		return (1);
	if (zpool_get_state(zhp) != POOL_STATE_UNAVAIL &&
	    zpool_enable_datasets(zhp, mntopts, 0) != 0) {
		ret = 1;
		(void) fprintf(stderr, gettext("Split was succssful, but "
		    "the datasets could not all be mounted\n"));
		(void) fprintf(stderr, gettext("Try doing '%s' with a "
		    "different altroot\n"), "zpool import");
	}
	zpool_close(zhp);

	return (ret);
}



/*
 * zpool online <pool> <device> ...
 */
int
zpool_do_online(int argc, char **argv)
{
	int c, i;
	char *poolname;
	zpool_handle_t *zhp;
	int ret = 0;
	vdev_state_t newstate;
	int flags = 0;

	/* check options */
	while ((c = getopt(argc, argv, "et")) != -1) {
		switch (c) {
		case 'e':
			flags |= ZFS_ONLINE_EXPAND;
			break;
		case 't':
		case '?':
			(void) fprintf(stderr, gettext("invalid option '%c'\n"),
			    optopt);
			usage(B_FALSE);
		}
	}

	argc -= optind;
	argv += optind;

	/* get pool name and check number of arguments */
	if (argc < 1) {
		(void) fprintf(stderr, gettext("missing pool name\n"));
		usage(B_FALSE);
	}
	if (argc < 2) {
		(void) fprintf(stderr, gettext("missing device name\n"));
		usage(B_FALSE);
	}

	poolname = argv[0];

	if ((zhp = zpool_open(g_zfs, poolname)) == NULL)
		return (1);

	for (i = 1; i < argc; i++) {
		if (zpool_vdev_online(zhp, argv[i], flags, &newstate) == 0) {
			if (newstate != VDEV_STATE_HEALTHY) {
				(void) printf(gettext("warning: device '%s' "
				    "onlined, but remains in faulted state\n"),
				    argv[i]);
				if (newstate == VDEV_STATE_FAULTED)
					(void) printf(gettext("use 'zpool "
					    "clear' to restore a faulted "
					    "device\n"));
				else
					(void) printf(gettext("use 'zpool "
					    "replace' to replace devices "
					    "that are no longer present\n"));
			}
		} else {
			ret = 1;
		}
	}

	zpool_close(zhp);

	return (ret);
}

/*
 * zpool offline [-ft] <pool> <device> ...
 *
 *	-f	Force the device into the offline state, even if doing
 *		so would appear to compromise pool availability.
 *		(not supported yet)
 *
 *	-t	Only take the device off-line temporarily.  The offline
 *		state will not be persistent across reboots.
 */
/* ARGSUSED */
int
zpool_do_offline(int argc, char **argv)
{
	int c, i;
	char *poolname;
	zpool_handle_t *zhp;
	int ret = 0;
	boolean_t istmp = B_FALSE;

	/* check options */
	while ((c = getopt(argc, argv, "ft")) != -1) {
		switch (c) {
		case 't':
			istmp = B_TRUE;
			break;
		case 'f':
		case '?':
			(void) fprintf(stderr, gettext("invalid option '%c'\n"),
			    optopt);
			usage(B_FALSE);
		}
	}

	argc -= optind;
	argv += optind;

	/* get pool name and check number of arguments */
	if (argc < 1) {
		(void) fprintf(stderr, gettext("missing pool name\n"));
		usage(B_FALSE);
	}
	if (argc < 2) {
		(void) fprintf(stderr, gettext("missing device name\n"));
		usage(B_FALSE);
	}

	poolname = argv[0];

	if ((zhp = zpool_open(g_zfs, poolname)) == NULL)
		return (1);

	for (i = 1; i < argc; i++) {
		if (zpool_vdev_offline(zhp, argv[i], istmp) != 0)
			ret = 1;
	}

	zpool_close(zhp);

	return (ret);
}

/*
 * zpool clear <pool> [device]
 *
 * Clear all errors associated with a pool or a particular device.
 */
int
zpool_do_clear(int argc, char **argv)
{
	int c;
	int ret = 0;
	boolean_t dryrun = B_FALSE;
	boolean_t do_rewind = B_FALSE;
	boolean_t xtreme_rewind = B_FALSE;
	uint32_t rewind_policy = ZPOOL_NO_REWIND;
	nvlist_t *policy = NULL;
	zpool_handle_t *zhp;
	char *pool, *device;

	/* check options */
	while ((c = getopt(argc, argv, "FnX")) != -1) {
		switch (c) {
		case 'F':
			do_rewind = B_TRUE;
			break;
		case 'n':
			dryrun = B_TRUE;
			break;
		case 'X':
			xtreme_rewind = B_TRUE;
			break;
		case '?':
			(void) fprintf(stderr, gettext("invalid option '%c'\n"),
			    optopt);
			usage(B_FALSE);
		}
	}

	argc -= optind;
	argv += optind;

	if (argc < 1) {
		(void) fprintf(stderr, gettext("missing pool name\n"));
		usage(B_FALSE);
	}

	if (argc > 2) {
		(void) fprintf(stderr, gettext("too many arguments\n"));
		usage(B_FALSE);
	}

	if ((dryrun || xtreme_rewind) && !do_rewind) {
		(void) fprintf(stderr,
		    gettext("-n or -X only meaningful with -F\n"));
		usage(B_FALSE);
	}
	if (dryrun)
		rewind_policy = ZPOOL_TRY_REWIND;
	else if (do_rewind)
		rewind_policy = ZPOOL_DO_REWIND;
	if (xtreme_rewind)
		rewind_policy |= ZPOOL_EXTREME_REWIND;

	/* In future, further rewind policy choices can be passed along here */
	if (nvlist_alloc(&policy, NV_UNIQUE_NAME, 0) != 0 ||
	    nvlist_add_uint32(policy, ZPOOL_REWIND_REQUEST, rewind_policy) != 0)
		return (1);

	pool = argv[0];
	device = argc == 2 ? argv[1] : NULL;

	if ((zhp = zpool_open_canfail(g_zfs, pool)) == NULL) {
		nvlist_free(policy);
		return (1);
	}

	if (zpool_clear(zhp, device, policy) != 0)
		ret = 1;

	zpool_close(zhp);

	nvlist_free(policy);

	return (ret);
}

typedef struct scrub_cbdata {
	int	cb_type;
	int	cb_argc;
	char	**cb_argv;
} scrub_cbdata_t;

int
scrub_callback(zpool_handle_t *zhp, void *data)
{
	scrub_cbdata_t *cb = data;
	int err;

	/*
	 * Ignore faulted pools.
	 */
	if (zpool_get_state(zhp) == POOL_STATE_UNAVAIL) {
		(void) fprintf(stderr, gettext("cannot scrub '%s': pool is "
		    "currently unavailable\n"), zpool_get_name(zhp));
		return (1);
	}

	err = zpool_scan(zhp, cb->cb_type);

	return (err != 0);
}

/*
 * zpool scrub [-s] <pool> ...
 *
 *	-s	Stop.  Stops any in-progress scrub.
 */
int
zpool_do_scrub(int argc, char **argv)
{
	int c;
	scrub_cbdata_t cb;

	cb.cb_type = POOL_SCAN_SCRUB;

	/* check options */
	while ((c = getopt(argc, argv, "s")) != -1) {
		switch (c) {
		case 's':
			cb.cb_type = POOL_SCAN_NONE;
			break;
		case '?':
			(void) fprintf(stderr, gettext("invalid option '%c'\n"),
			    optopt);
			usage(B_FALSE);
		}
	}

	cb.cb_argc = argc;
	cb.cb_argv = argv;
	argc -= optind;
	argv += optind;

	if (argc < 1) {
		(void) fprintf(stderr, gettext("missing pool name argument\n"));
		usage(B_FALSE);
	}

	return (for_each_pool(argc, argv, B_TRUE, NULL, scrub_callback, &cb));
}

typedef struct status_cbdata {
	int		cb_count;
	boolean_t	cb_allpools;
	boolean_t	cb_verbose;
	boolean_t	cb_explain;
	boolean_t	cb_first;
	boolean_t	cb_dedup_stats;
} status_cbdata_t;

/*
 * Print out detailed scrub status.
 */
void
print_scan_status(pool_scan_stat_t *ps)
{
	time_t start, end;
	uint64_t elapsed, mins_left, hours_left;
	uint64_t pass_exam, examined, total;
	uint_t rate;
	double fraction_done;
	char processed_buf[7], examined_buf[7], total_buf[7], rate_buf[7];

	(void) printf(gettext(" scan: "));

	/* If there's never been a scan, there's not much to say. */
	if (ps == NULL || ps->pss_func == POOL_SCAN_NONE ||
	    ps->pss_func >= POOL_SCAN_FUNCS) {
		(void) printf(gettext("none requested\n"));
		return;
	}

	start = ps->pss_start_time;
	end = ps->pss_end_time;
	zfs_nicenum(ps->pss_processed, processed_buf, sizeof (processed_buf));

	assert(ps->pss_func == POOL_SCAN_SCRUB ||
	    ps->pss_func == POOL_SCAN_RESILVER);
	/*
	 * Scan is finished or canceled.
	 */
	if (ps->pss_state == DSS_FINISHED) {
		uint64_t minutes_taken = (end - start) / 60;
		char *fmt;

		if (ps->pss_func == POOL_SCAN_SCRUB) {
			fmt = gettext("scrub repaired %s in %lluh%um with "
			    "%llu errors on %s");
		} else if (ps->pss_func == POOL_SCAN_RESILVER) {
			fmt = gettext("resilvered %s in %lluh%um with "
			    "%llu errors on %s");
		}
		/* LINTED */
		(void) printf(fmt, processed_buf,
		    (u_longlong_t)(minutes_taken / 60),
		    (uint_t)(minutes_taken % 60),
		    (u_longlong_t)ps->pss_errors,
		    ctime((time_t *)&end));
		return;
	} else if (ps->pss_state == DSS_CANCELED) {
		if (ps->pss_func == POOL_SCAN_SCRUB) {
			(void) printf(gettext("scrub canceled on %s"),
			    ctime(&end));
		} else if (ps->pss_func == POOL_SCAN_RESILVER) {
			(void) printf(gettext("resilver canceled on %s"),
			    ctime(&end));
		}
		return;
	}

	assert(ps->pss_state == DSS_SCANNING);

	/*
	 * Scan is in progress.
	 */
	if (ps->pss_func == POOL_SCAN_SCRUB) {
		(void) printf(gettext("scrub in progress since %s"),
		    ctime(&start));
	} else if (ps->pss_func == POOL_SCAN_RESILVER) {
		(void) printf(gettext("resilver in progress since %s"),
		    ctime(&start));
	}

	examined = ps->pss_examined ? ps->pss_examined : 1;
	total = ps->pss_to_examine;
	fraction_done = (double)examined / total;

	/* elapsed time for this pass */
	elapsed = time(NULL) - ps->pss_pass_start;
	elapsed = elapsed ? elapsed : 1;
	pass_exam = ps->pss_pass_exam ? ps->pss_pass_exam : 1;
	rate = pass_exam / elapsed;
	rate = rate ? rate : 1;
	mins_left = ((total - examined) / rate) / 60;
	hours_left = mins_left / 60;

	zfs_nicenum(examined, examined_buf, sizeof (examined_buf));
	zfs_nicenum(total, total_buf, sizeof (total_buf));
	zfs_nicenum(rate, rate_buf, sizeof (rate_buf));

	/*
	 * do not print estimated time if hours_left is more than 30 days
	 */
	(void) printf(gettext("    %s scanned out of %s at %s/s"),
	    examined_buf, total_buf, rate_buf);
	if (hours_left < (30 * 24)) {
		(void) printf(gettext(", %lluh%um to go\n"),
		    (u_longlong_t)hours_left, (uint_t)(mins_left % 60));
	} else {
		(void) printf(gettext(
		    ", (scan is slow, no estimated time)\n"));
	}

	if (ps->pss_func == POOL_SCAN_RESILVER) {
		(void) printf(gettext("    %s resilvered, %.2f%% done\n"),
		    processed_buf, 100 * fraction_done);
	} else if (ps->pss_func == POOL_SCAN_SCRUB) {
		(void) printf(gettext("    %s repaired, %.2f%% done\n"),
		    processed_buf, 100 * fraction_done);
	}
}

static void
print_error_log(zpool_handle_t *zhp)
{
	nvlist_t *nverrlist = NULL;
	nvpair_t *elem;
	char *pathname;
	size_t len = MAXPATHLEN * 2;

	if (zpool_get_errlog(zhp, &nverrlist) != 0) {
		(void) printf("errors: List of errors unavailable "
		    "(insufficient privileges)\n");
		return;
	}

	(void) printf("errors: Permanent errors have been "
	    "detected in the following files:\n\n");

	pathname = safe_malloc(len);
	elem = NULL;
	while ((elem = nvlist_next_nvpair(nverrlist, elem)) != NULL) {
		nvlist_t *nv;
		uint64_t dsobj, obj;

		verify(nvpair_value_nvlist(elem, &nv) == 0);
		verify(nvlist_lookup_uint64(nv, ZPOOL_ERR_DATASET,
		    &dsobj) == 0);
		verify(nvlist_lookup_uint64(nv, ZPOOL_ERR_OBJECT,
		    &obj) == 0);
		zpool_obj_to_path(zhp, dsobj, obj, pathname, len);
		(void) printf("%7s %s\n", "", pathname);
	}
	free(pathname);
	nvlist_free(nverrlist);
}

static void
print_spares(zpool_handle_t *zhp, nvlist_t **spares, uint_t nspares,
    int namewidth)
{
	uint_t i;
	char *name;

	if (nspares == 0)
		return;

	(void) printf(gettext("\tspares\n"));

	for (i = 0; i < nspares; i++) {
		name = zpool_vdev_name(g_zfs, zhp, spares[i], B_FALSE);
		print_status_config(zhp, name, spares[i],
		    namewidth, 2, B_TRUE);
		free(name);
	}
}

static void
print_l2cache(zpool_handle_t *zhp, nvlist_t **l2cache, uint_t nl2cache,
    int namewidth)
{
	uint_t i;
	char *name;

	if (nl2cache == 0)
		return;

	(void) printf(gettext("\tcache\n"));

	for (i = 0; i < nl2cache; i++) {
		name = zpool_vdev_name(g_zfs, zhp, l2cache[i], B_FALSE);
		print_status_config(zhp, name, l2cache[i],
		    namewidth, 2, B_FALSE);
		free(name);
	}
}

static void
print_dedup_stats(nvlist_t *config)
{
	ddt_histogram_t *ddh;
	ddt_stat_t *dds;
	ddt_object_t *ddo;
	uint_t c;

	/*
	 * If the pool was faulted then we may not have been able to
	 * obtain the config. Otherwise, if have anything in the dedup
	 * table continue processing the stats.
	 */
	if (nvlist_lookup_uint64_array(config, ZPOOL_CONFIG_DDT_OBJ_STATS,
	    (uint64_t **)&ddo, &c) != 0 || ddo->ddo_count == 0)
		return;

	(void) printf("\n");
	(void) printf("DDT entries %llu, size %llu on disk, %llu in core\n",
	    (u_longlong_t)ddo->ddo_count,
	    (u_longlong_t)ddo->ddo_dspace,
	    (u_longlong_t)ddo->ddo_mspace);

	verify(nvlist_lookup_uint64_array(config, ZPOOL_CONFIG_DDT_STATS,
	    (uint64_t **)&dds, &c) == 0);
	verify(nvlist_lookup_uint64_array(config, ZPOOL_CONFIG_DDT_HISTOGRAM,
	    (uint64_t **)&ddh, &c) == 0);
	zpool_dump_ddt(dds, ddh);
}

/*
 * Display a summary of pool status.  Displays a summary such as:
 *
 *        pool: tank
 *	status: DEGRADED
 *	reason: One or more devices ...
 *         see: http://www.sun.com/msg/ZFS-xxxx-01
 *	config:
 *		mirror		DEGRADED
 *                c1t0d0	OK
 *                c2t0d0	UNAVAIL
 *
 * When given the '-v' option, we print out the complete config.  If the '-e'
 * option is specified, then we print out error rate information as well.
 */
int
status_callback(zpool_handle_t *zhp, void *data)
{
	status_cbdata_t *cbp = data;
	nvlist_t *config, *nvroot;
	char *msgid;
	int reason;
	const char *health;
	uint_t c;
	vdev_stat_t *vs;

	config = zpool_get_config(zhp, NULL);
	reason = zpool_get_status(zhp, &msgid);

	cbp->cb_count++;

	/*
	 * If we were given 'zpool status -x', only report those pools with
	 * problems.
	 */
	if (reason == ZPOOL_STATUS_OK && cbp->cb_explain) {
		if (!cbp->cb_allpools) {
			(void) printf(gettext("pool '%s' is healthy\n"),
			    zpool_get_name(zhp));
			if (cbp->cb_first)
				cbp->cb_first = B_FALSE;
		}
		return (0);
	}

	if (cbp->cb_first)
		cbp->cb_first = B_FALSE;
	else
		(void) printf("\n");

	verify(nvlist_lookup_nvlist(config, ZPOOL_CONFIG_VDEV_TREE,
	    &nvroot) == 0);
	verify(nvlist_lookup_uint64_array(nvroot, ZPOOL_CONFIG_VDEV_STATS,
	    (uint64_t **)&vs, &c) == 0);
	health = zpool_state_to_name(vs->vs_state, vs->vs_aux);

	(void) printf(gettext("  pool: %s\n"), zpool_get_name(zhp));
	(void) printf(gettext(" state: %s\n"), health);

	switch (reason) {
	case ZPOOL_STATUS_MISSING_DEV_R:
		(void) printf(gettext("status: One or more devices could not "
		    "be opened.  Sufficient replicas exist for\n\tthe pool to "
		    "continue functioning in a degraded state.\n"));
		(void) printf(gettext("action: Attach the missing device and "
		    "online it using 'zpool online'.\n"));
		break;

	case ZPOOL_STATUS_MISSING_DEV_NR:
		(void) printf(gettext("status: One or more devices could not "
		    "be opened.  There are insufficient\n\treplicas for the "
		    "pool to continue functioning.\n"));
		(void) printf(gettext("action: Attach the missing device and "
		    "online it using 'zpool online'.\n"));
		break;

	case ZPOOL_STATUS_CORRUPT_LABEL_R:
		(void) printf(gettext("status: One or more devices could not "
		    "be used because the label is missing or\n\tinvalid.  "
		    "Sufficient replicas exist for the pool to continue\n\t"
		    "functioning in a degraded state.\n"));
		(void) printf(gettext("action: Replace the device using "
		    "'zpool replace'.\n"));
		break;

	case ZPOOL_STATUS_CORRUPT_LABEL_NR:
		(void) printf(gettext("status: One or more devices could not "
		    "be used because the label is missing \n\tor invalid.  "
		    "There are insufficient replicas for the pool to "
		    "continue\n\tfunctioning.\n"));
		zpool_explain_recover(zpool_get_handle(zhp),
		    zpool_get_name(zhp), reason, config);
		break;

	case ZPOOL_STATUS_FAILING_DEV:
		(void) printf(gettext("status: One or more devices has "
		    "experienced an unrecoverable error.  An\n\tattempt was "
		    "made to correct the error.  Applications are "
		    "unaffected.\n"));
		(void) printf(gettext("action: Determine if the device needs "
		    "to be replaced, and clear the errors\n\tusing "
		    "'zpool clear' or replace the device with 'zpool "
		    "replace'.\n"));
		break;

	case ZPOOL_STATUS_OFFLINE_DEV:
		(void) printf(gettext("status: One or more devices has "
		    "been taken offline by the administrator.\n\tSufficient "
		    "replicas exist for the pool to continue functioning in "
		    "a\n\tdegraded state.\n"));
		(void) printf(gettext("action: Online the device using "
		    "'zpool online' or replace the device with\n\t'zpool "
		    "replace'.\n"));
		break;

	case ZPOOL_STATUS_REMOVED_DEV:
		(void) printf(gettext("status: One or more devices has "
		    "been removed by the administrator.\n\tSufficient "
		    "replicas exist for the pool to continue functioning in "
		    "a\n\tdegraded state.\n"));
		(void) printf(gettext("action: Online the device using "
		    "'zpool online' or replace the device with\n\t'zpool "
		    "replace'.\n"));
		break;

	case ZPOOL_STATUS_RESILVERING:
		(void) printf(gettext("status: One or more devices is "
		    "currently being resilvered.  The pool will\n\tcontinue "
		    "to function, possibly in a degraded state.\n"));
		(void) printf(gettext("action: Wait for the resilver to "
		    "complete.\n"));
		break;

	case ZPOOL_STATUS_CORRUPT_DATA:
		(void) printf(gettext("status: One or more devices has "
		    "experienced an error resulting in data\n\tcorruption.  "
		    "Applications may be affected.\n"));
		(void) printf(gettext("action: Restore the file in question "
		    "if possible.  Otherwise restore the\n\tentire pool from "
		    "backup.\n"));
		break;

	case ZPOOL_STATUS_CORRUPT_POOL:
		(void) printf(gettext("status: The pool metadata is corrupted "
		    "and the pool cannot be opened.\n"));
		zpool_explain_recover(zpool_get_handle(zhp),
		    zpool_get_name(zhp), reason, config);
		break;

	case ZPOOL_STATUS_VERSION_OLDER:
		(void) printf(gettext("status: The pool is formatted using an "
		    "older on-disk format.  The pool can\n\tstill be used, but "
		    "some features are unavailable.\n"));
		(void) printf(gettext("action: Upgrade the pool using 'zpool "
		    "upgrade'.  Once this is done, the\n\tpool will no longer "
		    "be accessible on older software versions.\n"));
		break;

	case ZPOOL_STATUS_VERSION_NEWER:
		(void) printf(gettext("status: The pool has been upgraded to a "
		    "newer, incompatible on-disk version.\n\tThe pool cannot "
		    "be accessed on this system.\n"));
		(void) printf(gettext("action: Access the pool from a system "
		    "running more recent software, or\n\trestore the pool from "
		    "backup.\n"));
		break;

	case ZPOOL_STATUS_FAULTED_DEV_R:
		(void) printf(gettext("status: One or more devices are "
		    "faulted in response to persistent errors.\n\tSufficient "
		    "replicas exist for the pool to continue functioning "
		    "in a\n\tdegraded state.\n"));
		(void) printf(gettext("action: Replace the faulted device, "
		    "or use 'zpool clear' to mark the device\n\trepaired.\n"));
		break;

	case ZPOOL_STATUS_FAULTED_DEV_NR:
		(void) printf(gettext("status: One or more devices are "
		    "faulted in response to persistent errors.  There are "
		    "insufficient replicas for the pool to\n\tcontinue "
		    "functioning.\n"));
		(void) printf(gettext("action: Destroy and re-create the pool "
		    "from a backup source.  Manually marking the device\n"
		    "\trepaired using 'zpool clear' may allow some data "
		    "to be recovered.\n"));
		break;

	case ZPOOL_STATUS_IO_FAILURE_WAIT:
	case ZPOOL_STATUS_IO_FAILURE_CONTINUE:
		(void) printf(gettext("status: One or more devices are "
		    "faulted in response to IO failures.\n"));
		(void) printf(gettext("action: Make sure the affected devices "
		    "are connected, then run 'zpool clear'.\n"));
		break;

	case ZPOOL_STATUS_BAD_LOG:
		(void) printf(gettext("status: An intent log record "
		    "could not be read.\n"
		    "\tWaiting for adminstrator intervention to fix the "
		    "faulted pool.\n"));
		(void) printf(gettext("action: Either restore the affected "
		    "device(s) and run 'zpool online',\n"
		    "\tor ignore the intent log records by running "
		    "'zpool clear'.\n"));
		break;

	default:
		/*
		 * The remaining errors can't actually be generated, yet.
		 */
		assert(reason == ZPOOL_STATUS_OK);
	}

	if (msgid != NULL)
		(void) printf(gettext("   see: http://www.sun.com/msg/%s\n"),
		    msgid);

	if (config != NULL) {
		int namewidth;
		uint64_t nerr;
		nvlist_t **spares, **l2cache;
		uint_t nspares, nl2cache;
		pool_scan_stat_t *ps = NULL;

		(void) nvlist_lookup_uint64_array(nvroot,
		    ZPOOL_CONFIG_SCAN_STATS, (uint64_t **)&ps, &c);
		print_scan_status(ps);

		namewidth = max_width(zhp, nvroot, 0, 0);
		if (namewidth < 10)
			namewidth = 10;

		(void) printf(gettext("config:\n\n"));
		(void) printf(gettext("\t%-*s  %-8s %5s %5s %5s\n"), namewidth,
		    "NAME", "STATE", "READ", "WRITE", "CKSUM");
		print_status_config(zhp, zpool_get_name(zhp), nvroot,
		    namewidth, 0, B_FALSE);

		if (num_logs(nvroot) > 0)
			print_logs(zhp, nvroot, namewidth, B_TRUE);
		if (nvlist_lookup_nvlist_array(nvroot, ZPOOL_CONFIG_L2CACHE,
		    &l2cache, &nl2cache) == 0)
			print_l2cache(zhp, l2cache, nl2cache, namewidth);

		if (nvlist_lookup_nvlist_array(nvroot, ZPOOL_CONFIG_SPARES,
		    &spares, &nspares) == 0)
			print_spares(zhp, spares, nspares, namewidth);

		if (nvlist_lookup_uint64(config, ZPOOL_CONFIG_ERRCOUNT,
		    &nerr) == 0) {
			nvlist_t *nverrlist = NULL;

			/*
			 * If the approximate error count is small, get a
			 * precise count by fetching the entire log and
			 * uniquifying the results.
			 */
			if (nerr > 0 && nerr < 100 && !cbp->cb_verbose &&
			    zpool_get_errlog(zhp, &nverrlist) == 0) {
				nvpair_t *elem;

				elem = NULL;
				nerr = 0;
				while ((elem = nvlist_next_nvpair(nverrlist,
				    elem)) != NULL) {
					nerr++;
				}
			}
			nvlist_free(nverrlist);

			(void) printf("\n");

			if (nerr == 0)
				(void) printf(gettext("errors: No known data "
				    "errors\n"));
			else if (!cbp->cb_verbose)
				(void) printf(gettext("errors: %llu data "
				    "errors, use '-v' for a list\n"),
				    (u_longlong_t)nerr);
			else
				print_error_log(zhp);
		}

		if (cbp->cb_dedup_stats)
			print_dedup_stats(config);
	} else {
		(void) printf(gettext("config: The configuration cannot be "
		    "determined.\n"));
	}

	return (0);
}

/*
 * zpool status [-vx] [-T d|u] [pool] ... [interval [count]]
 *
 *	-v	Display complete error logs
 *	-x	Display only pools with potential problems
 *	-D	Display dedup status (undocumented)
 *	-T	Display a timestamp in date(1) or Unix format
 *
 * Describes the health status of all pools or some subset.
 */
int
zpool_do_status(int argc, char **argv)
{
	int c;
	int ret;
	unsigned long interval = 0, count = 0;
	status_cbdata_t cb = { 0 };

	/* check options */
	while ((c = getopt(argc, argv, "vxDT:")) != -1) {
		switch (c) {
		case 'v':
			cb.cb_verbose = B_TRUE;
			break;
		case 'x':
			cb.cb_explain = B_TRUE;
			break;
		case 'D':
			cb.cb_dedup_stats = B_TRUE;
			break;
		case 'T':
			get_timestamp_arg(*optarg);
			break;
		case '?':
			(void) fprintf(stderr, gettext("invalid option '%c'\n"),
			    optopt);
			usage(B_FALSE);
		}
	}

	argc -= optind;
	argv += optind;

	get_interval_count(&argc, argv, &interval, &count);

	if (argc == 0)
		cb.cb_allpools = B_TRUE;

	cb.cb_first = B_TRUE;

	for (;;) {
		if (timestamp_fmt != NODATE)
			print_timestamp(timestamp_fmt);

		ret = for_each_pool(argc, argv, B_TRUE, NULL,
		    status_callback, &cb);

		if (argc == 0 && cb.cb_count == 0)
			(void) printf(gettext("no pools available\n"));
		else if (cb.cb_explain && cb.cb_first && cb.cb_allpools)
			(void) printf(gettext("all pools are healthy\n"));

		if (ret != 0)
			return (ret);

		if (interval == 0)
			break;

		if (count != 0 && --count == 0)
			break;

		(void) sleep(interval);
	}

	return (0);
}

typedef struct upgrade_cbdata {
	int	cb_all;
	int	cb_first;
	int	cb_newer;
	int	cb_argc;
	uint64_t cb_version;
	char	**cb_argv;
} upgrade_cbdata_t;

static int
upgrade_cb(zpool_handle_t *zhp, void *arg)
{
	upgrade_cbdata_t *cbp = arg;
	nvlist_t *config;
	uint64_t version;
	int ret = 0;

	config = zpool_get_config(zhp, NULL);
	verify(nvlist_lookup_uint64(config, ZPOOL_CONFIG_VERSION,
	    &version) == 0);

	if (!cbp->cb_newer && version < SPA_VERSION) {
		if (!cbp->cb_all) {
			if (cbp->cb_first) {
				(void) printf(gettext("The following pools are "
				    "out of date, and can be upgraded.  After "
				    "being\nupgraded, these pools will no "
				    "longer be accessible by older software "
				    "versions.\n\n"));
				(void) printf(gettext("VER  POOL\n"));
				(void) printf(gettext("---  ------------\n"));
				cbp->cb_first = B_FALSE;
			}

			(void) printf("%2llu   %s\n", (u_longlong_t)version,
			    zpool_get_name(zhp));
		} else {
			cbp->cb_first = B_FALSE;
			ret = zpool_upgrade(zhp, cbp->cb_version);
			if (!ret) {
				(void) printf(gettext("Successfully upgraded "
				    "'%s'\n\n"), zpool_get_name(zhp));
			}
		}
	} else if (cbp->cb_newer && version > SPA_VERSION) {
		assert(!cbp->cb_all);

		if (cbp->cb_first) {
			(void) printf(gettext("The following pools are "
			    "formatted using a newer software version and\n"
			    "cannot be accessed on the current system.\n\n"));
			(void) printf(gettext("VER  POOL\n"));
			(void) printf(gettext("---  ------------\n"));
			cbp->cb_first = B_FALSE;
		}

		(void) printf("%2llu   %s\n", (u_longlong_t)version,
		    zpool_get_name(zhp));
	}

	zpool_close(zhp);
	return (ret);
}

/* ARGSUSED */
static int
upgrade_one(zpool_handle_t *zhp, void *data)
{
	upgrade_cbdata_t *cbp = data;
	uint64_t cur_version;
	int ret;

	if (strcmp("log", zpool_get_name(zhp)) == 0) {
		(void) printf(gettext("'log' is now a reserved word\n"
		    "Pool 'log' must be renamed using export and import"
		    " to upgrade.\n"));
		return (1);
	}

	cur_version = zpool_get_prop_int(zhp, ZPOOL_PROP_VERSION, NULL);
	if (cur_version > cbp->cb_version) {
		(void) printf(gettext("Pool '%s' is already formatted "
		    "using more current version '%llu'.\n"),
		    zpool_get_name(zhp), cur_version);
		return (0);
	}
	if (cur_version == cbp->cb_version) {
		(void) printf(gettext("Pool '%s' is already formatted "
		    "using the current version.\n"), zpool_get_name(zhp));
		return (0);
	}

	ret = zpool_upgrade(zhp, cbp->cb_version);

	if (!ret) {
		(void) printf(gettext("Successfully upgraded '%s' "
		    "from version %llu to version %llu\n\n"),
		    zpool_get_name(zhp), (u_longlong_t)cur_version,
		    (u_longlong_t)cbp->cb_version);
	}

	return (ret != 0);
}

/*
 * zpool upgrade
 * zpool upgrade -v
 * zpool upgrade [-V version] <-a | pool ...>
 *
 * With no arguments, display downrev'd ZFS pool available for upgrade.
 * Individual pools can be upgraded by specifying the pool, and '-a' will
 * upgrade all pools.
 */
int
zpool_do_upgrade(int argc, char **argv)
{
	int c;
	upgrade_cbdata_t cb = { 0 };
	int ret = 0;
	boolean_t showversions = B_FALSE;
	char *end;


	/* check options */
	while ((c = getopt(argc, argv, ":avV:")) != -1) {
		switch (c) {
		case 'a':
			cb.cb_all = B_TRUE;
			break;
		case 'v':
			showversions = B_TRUE;
			break;
		case 'V':
			cb.cb_version = strtoll(optarg, &end, 10);
			if (*end != '\0' || cb.cb_version > SPA_VERSION ||
			    cb.cb_version < SPA_VERSION_1) {
				(void) fprintf(stderr,
				    gettext("invalid version '%s'\n"), optarg);
				usage(B_FALSE);
			}
			break;
		case ':':
			(void) fprintf(stderr, gettext("missing argument for "
			    "'%c' option\n"), optopt);
			usage(B_FALSE);
			break;
		case '?':
			(void) fprintf(stderr, gettext("invalid option '%c'\n"),
			    optopt);
			usage(B_FALSE);
		}
	}

	cb.cb_argc = argc;
	cb.cb_argv = argv;
	argc -= optind;
	argv += optind;

	if (cb.cb_version == 0) {
		cb.cb_version = SPA_VERSION;
	} else if (!cb.cb_all && argc == 0) {
		(void) fprintf(stderr, gettext("-V option is "
		    "incompatible with other arguments\n"));
		usage(B_FALSE);
	}

	if (showversions) {
		if (cb.cb_all || argc != 0) {
			(void) fprintf(stderr, gettext("-v option is "
			    "incompatible with other arguments\n"));
			usage(B_FALSE);
		}
	} else if (cb.cb_all) {
		if (argc != 0) {
			(void) fprintf(stderr, gettext("-a option should not "
			    "be used along with a pool name\n"));
			usage(B_FALSE);
		}
	}

	(void) printf(gettext("This system is currently running "
	    "ZFS pool version %llu.\n\n"), SPA_VERSION);
	cb.cb_first = B_TRUE;
	if (showversions) {
		(void) printf(gettext("The following versions are "
		    "supported:\n\n"));
		(void) printf(gettext("VER  DESCRIPTION\n"));
		(void) printf("---  -----------------------------------------"
		    "---------------\n");
		(void) printf(gettext(" 1   Initial ZFS version\n"));
		(void) printf(gettext(" 2   Ditto blocks "
		    "(replicated metadata)\n"));
		(void) printf(gettext(" 3   Hot spares and double parity "
		    "RAID-Z\n"));
		(void) printf(gettext(" 4   zpool history\n"));
		(void) printf(gettext(" 5   Compression using the gzip "
		    "algorithm\n"));
		(void) printf(gettext(" 6   bootfs pool property\n"));
		(void) printf(gettext(" 7   Separate intent log devices\n"));
		(void) printf(gettext(" 8   Delegated administration\n"));
		(void) printf(gettext(" 9   refquota and refreservation "
		    "properties\n"));
		(void) printf(gettext(" 10  Cache devices\n"));
		(void) printf(gettext(" 11  Improved scrub performance\n"));
		(void) printf(gettext(" 12  Snapshot properties\n"));
		(void) printf(gettext(" 13  snapused property\n"));
		(void) printf(gettext(" 14  passthrough-x aclinherit\n"));
		(void) printf(gettext(" 15  user/group space accounting\n"));
		(void) printf(gettext(" 16  stmf property support\n"));
		(void) printf(gettext(" 17  Triple-parity RAID-Z\n"));
		(void) printf(gettext(" 18  Snapshot user holds\n"));
		(void) printf(gettext(" 19  Log device removal\n"));
		(void) printf(gettext(" 20  Compression using zle "
		    "(zero-length encoding)\n"));
		(void) printf(gettext(" 21  Deduplication\n"));
		(void) printf(gettext(" 22  Received properties\n"));
		(void) printf(gettext(" 23  Slim ZIL\n"));
		(void) printf(gettext(" 24  System attributes\n"));
		(void) printf(gettext(" 25  Improved scrub stats\n"));
		(void) printf(gettext(" 26  Improved snapshot deletion "
		    "performance\n"));
		(void) printf(gettext(" 27  Improved snapshot creation "
		    "performance\n"));
		(void) printf(gettext(" 28  Multiple vdev replacements\n"));
		(void) printf(gettext("\nFor more information on a particular "
		    "version, including supported releases,\n"));
		(void) printf(gettext("see the ZFS Administration Guide.\n\n"));
	} else if (argc == 0) {
		int notfound;

		ret = zpool_iter(g_zfs, upgrade_cb, &cb);
		notfound = cb.cb_first;

		if (!cb.cb_all && ret == 0) {
			if (!cb.cb_first)
				(void) printf("\n");
			cb.cb_first = B_TRUE;
			cb.cb_newer = B_TRUE;
			ret = zpool_iter(g_zfs, upgrade_cb, &cb);
			if (!cb.cb_first) {
				notfound = B_FALSE;
				(void) printf("\n");
			}
		}

		if (ret == 0) {
			if (notfound)
				(void) printf(gettext("All pools are formatted "
				    "using this version.\n"));
			else if (!cb.cb_all)
				(void) printf(gettext("Use 'zpool upgrade -v' "
				    "for a list of available versions and "
				    "their associated\nfeatures.\n"));
		}
	} else {
		ret = for_each_pool(argc, argv, B_FALSE, NULL,
		    upgrade_one, &cb);
	}

	return (ret);
}

typedef struct hist_cbdata {
	boolean_t first;
	int longfmt;
	int internal;
} hist_cbdata_t;

/*
 * Print out the command history for a specific pool.
 */
static int
get_history_one(zpool_handle_t *zhp, void *data)
{
	nvlist_t *nvhis;
	nvlist_t **records;
	uint_t numrecords;
	char *cmdstr;
	char *pathstr;
	uint64_t dst_time;
	time_t tsec;
	struct tm t;
	char tbuf[30];
	int ret, i;
	uint64_t who;
	struct passwd *pwd;
	char *hostname;
	char *zonename;
	char internalstr[MAXPATHLEN];
	hist_cbdata_t *cb = (hist_cbdata_t *)data;
	uint64_t txg;
	uint64_t ievent;

	cb->first = B_FALSE;

	(void) printf(gettext("History for '%s':\n"), zpool_get_name(zhp));

	if ((ret = zpool_get_history(zhp, &nvhis)) != 0)
		return (ret);

	verify(nvlist_lookup_nvlist_array(nvhis, ZPOOL_HIST_RECORD,
	    &records, &numrecords) == 0);
	for (i = 0; i < numrecords; i++) {
		if (nvlist_lookup_uint64(records[i], ZPOOL_HIST_TIME,
		    &dst_time) != 0)
			continue;

		/* is it an internal event or a standard event? */
		if (nvlist_lookup_string(records[i], ZPOOL_HIST_CMD,
		    &cmdstr) != 0) {
			if (cb->internal == 0)
				continue;

			if (nvlist_lookup_uint64(records[i],
			    ZPOOL_HIST_INT_EVENT, &ievent) != 0)
				continue;
			verify(nvlist_lookup_uint64(records[i],
			    ZPOOL_HIST_TXG, &txg) == 0);
			verify(nvlist_lookup_string(records[i],
			    ZPOOL_HIST_INT_STR, &pathstr) == 0);
			if (ievent >= LOG_END)
				continue;
			(void) snprintf(internalstr,
			    sizeof (internalstr),
			    "[internal %s txg:%lld] %s",
			    zfs_history_event_names[ievent], txg,
			    pathstr);
			cmdstr = internalstr;
		}
		tsec = dst_time;
		(void) localtime_r(&tsec, &t);
		(void) strftime(tbuf, sizeof (tbuf), "%F.%T", &t);
		(void) printf("%s %s", tbuf, cmdstr);

		if (!cb->longfmt) {
			(void) printf("\n");
			continue;
		}
		(void) printf(" [");
		if (nvlist_lookup_uint64(records[i],
		    ZPOOL_HIST_WHO, &who) == 0) {
			pwd = getpwuid((uid_t)who);
			if (pwd)
				(void) printf("user %s on",
				    pwd->pw_name);
			else
				(void) printf("user %d on",
				    (int)who);
		} else {
			(void) printf(gettext("no info]\n"));
			continue;
		}
		if (nvlist_lookup_string(records[i],
		    ZPOOL_HIST_HOST, &hostname) == 0) {
			(void) printf(" %s", hostname);
		}
		if (nvlist_lookup_string(records[i],
		    ZPOOL_HIST_ZONE, &zonename) == 0) {
			(void) printf(":%s", zonename);
		}

		(void) printf("]");
		(void) printf("\n");
	}
	(void) printf("\n");
	nvlist_free(nvhis);

	return (ret);
}

/*
 * zpool history <pool>
 *
 * Displays the history of commands that modified pools.
 */


int
zpool_do_history(int argc, char **argv)
{
	hist_cbdata_t cbdata = { 0 };
	int ret;
	int c;

	cbdata.first = B_TRUE;
	/* check options */
	while ((c = getopt(argc, argv, "li")) != -1) {
		switch (c) {
		case 'l':
			cbdata.longfmt = 1;
			break;
		case 'i':
			cbdata.internal = 1;
			break;
		case '?':
			(void) fprintf(stderr, gettext("invalid option '%c'\n"),
			    optopt);
			usage(B_FALSE);
		}
	}
	argc -= optind;
	argv += optind;

	ret = for_each_pool(argc, argv, B_FALSE,  NULL, get_history_one,
	    &cbdata);

	if (argc == 0 && cbdata.first == B_TRUE) {
		(void) printf(gettext("no pools available\n"));
		return (0);
	}

	return (ret);
}

static int
get_callback(zpool_handle_t *zhp, void *data)
{
	zprop_get_cbdata_t *cbp = (zprop_get_cbdata_t *)data;
	char value[MAXNAMELEN];
	zprop_source_t srctype;
	zprop_list_t *pl;

	for (pl = cbp->cb_proplist; pl != NULL; pl = pl->pl_next) {

		/*
		 * Skip the special fake placeholder. This will also skip
		 * over the name property when 'all' is specified.
		 */
		if (pl->pl_prop == ZPOOL_PROP_NAME &&
		    pl == cbp->cb_proplist)
			continue;

		if (zpool_get_prop(zhp, pl->pl_prop,
		    value, sizeof (value), &srctype) != 0)
			continue;

		zprop_print_one_property(zpool_get_name(zhp), cbp,
		    zpool_prop_to_name(pl->pl_prop), value, srctype, NULL,
		    NULL);
	}
	return (0);
}

int
zpool_do_get(int argc, char **argv)
{
	zprop_get_cbdata_t cb = { 0 };
	zprop_list_t fake_name = { 0 };
	int ret;

	if (argc < 3)
		usage(B_FALSE);

	cb.cb_first = B_TRUE;
	cb.cb_sources = ZPROP_SRC_ALL;
	cb.cb_columns[0] = GET_COL_NAME;
	cb.cb_columns[1] = GET_COL_PROPERTY;
	cb.cb_columns[2] = GET_COL_VALUE;
	cb.cb_columns[3] = GET_COL_SOURCE;
	cb.cb_type = ZFS_TYPE_POOL;

	if (zprop_get_list(g_zfs, argv[1],  &cb.cb_proplist,
	    ZFS_TYPE_POOL) != 0)
		usage(B_FALSE);

	if (cb.cb_proplist != NULL) {
		fake_name.pl_prop = ZPOOL_PROP_NAME;
		fake_name.pl_width = strlen(gettext("NAME"));
		fake_name.pl_next = cb.cb_proplist;
		cb.cb_proplist = &fake_name;
	}

	ret = for_each_pool(argc - 2, argv + 2, B_TRUE, &cb.cb_proplist,
	    get_callback, &cb);

	if (cb.cb_proplist == &fake_name)
		zprop_free_list(fake_name.pl_next);
	else
		zprop_free_list(cb.cb_proplist);

	return (ret);
}

typedef struct set_cbdata {
	char *cb_propname;
	char *cb_value;
	boolean_t cb_any_successful;
} set_cbdata_t;

int
set_callback(zpool_handle_t *zhp, void *data)
{
	int error;
	set_cbdata_t *cb = (set_cbdata_t *)data;

	error = zpool_set_prop(zhp, cb->cb_propname, cb->cb_value);

	if (!error)
		cb->cb_any_successful = B_TRUE;

	return (error);
}

int
zpool_do_set(int argc, char **argv)
{
	set_cbdata_t cb = { 0 };
	int error;

	if (argc > 1 && argv[1][0] == '-') {
		(void) fprintf(stderr, gettext("invalid option '%c'\n"),
		    argv[1][1]);
		usage(B_FALSE);
	}

	if (argc < 2) {
		(void) fprintf(stderr, gettext("missing property=value "
		    "argument\n"));
		usage(B_FALSE);
	}

	if (argc < 3) {
		(void) fprintf(stderr, gettext("missing pool name\n"));
		usage(B_FALSE);
	}

	if (argc > 3) {
		(void) fprintf(stderr, gettext("too many pool names\n"));
		usage(B_FALSE);
	}

	cb.cb_propname = argv[1];
	cb.cb_value = strchr(cb.cb_propname, '=');
	if (cb.cb_value == NULL) {
		(void) fprintf(stderr, gettext("missing value in "
		    "property=value argument\n"));
		usage(B_FALSE);
	}

	*(cb.cb_value) = '\0';
	cb.cb_value++;

	error = for_each_pool(argc - 2, argv + 2, B_TRUE, NULL,
	    set_callback, &cb);

	return (error);
}

static int
find_command_idx(char *command, int *idx)
{
	int i;

	for (i = 0; i < NCOMMAND; i++) {
		if (command_table[i].name == NULL)
			continue;

		if (strcmp(command, command_table[i].name) == 0) {
			*idx = i;
			return (0);
		}
	}
	return (1);
}

int
main(int argc, char **argv)
{
	int ret;
	int i;
	char *cmdname;

	(void) setlocale(LC_ALL, "");
	(void) textdomain(TEXT_DOMAIN);

	if ((g_zfs = libzfs_init()) == NULL) {
		(void) fprintf(stderr, gettext("internal error: failed to "
		    "initialize ZFS library\n"));
		return (1);
	}

	libzfs_print_on_error(g_zfs, B_TRUE);

	opterr = 0;

	/*
	 * Make sure the user has specified some command.
	 */
	if (argc < 2) {
		(void) fprintf(stderr, gettext("missing command\n"));
		usage(B_FALSE);
	}

	cmdname = argv[1];

	/*
	 * Special case '-?'
	 */
	if (strcmp(cmdname, "-?") == 0)
		usage(B_TRUE);

	zpool_set_history_str("zpool", argc, argv, history_str);
	verify(zpool_stage_history(g_zfs, history_str) == 0);

	/*
	 * Run the appropriate command.
	 */
	if (find_command_idx(cmdname, &i) == 0) {
		current_command = &command_table[i];
		ret = command_table[i].func(argc - 1, argv + 1);
	} else if (strchr(cmdname, '=')) {
		verify(find_command_idx("set", &i) == 0);
		current_command = &command_table[i];
		ret = command_table[i].func(argc, argv);
	} else if (strcmp(cmdname, "freeze") == 0 && argc == 3) {
		/*
		 * 'freeze' is a vile debugging abomination, so we treat
		 * it as such.
		 */
		char buf[16384];
		int fd = open(ZFS_DEV, O_RDWR);
		(void) strcpy((void *)buf, argv[2]);
		return (!!ioctl(fd, ZFS_IOC_POOL_FREEZE, buf));
	} else {
		(void) fprintf(stderr, gettext("unrecognized "
		    "command '%s'\n"), cmdname);
		usage(B_FALSE);
	}

	libzfs_fini(g_zfs);

	/*
	 * The 'ZFS_ABORT' environment variable causes us to dump core on exit
	 * for the purposes of running ::findleaks.
	 */
	if (getenv("ZFS_ABORT") != NULL) {
		(void) printf("dumping core by request\n");
		abort();
	}

	return (ret);
}
