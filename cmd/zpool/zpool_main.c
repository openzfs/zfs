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
 * Copyright 2011 Nexenta Systems, Inc. All rights reserved.
 * Copyright (c) 2011, 2015 by Delphix. All rights reserved.
 * Copyright (c) 2012 by Frederik Wessels. All rights reserved.
 * Copyright (c) 2012 by Cyril Plisko. All rights reserved.
 * Copyright (c) 2013 by Prasad Joshi (sTec). All rights reserved.
 * Copyright 2016 Igor Kozhukhov <ikozhukhov@gmail.com>.
 * Copyright (c) 2017 Datto Inc.
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
#include <pwd.h>
#include <zone.h>
#include <sys/wait.h>
#include <zfs_prop.h>
#include <sys/fs/zfs.h>
#include <sys/stat.h>
#include <sys/fm/fs/zfs.h>
#include <sys/fm/util.h>
#include <sys/fm/protocol.h>
#include <sys/zfs_ioctl.h>

#include <math.h>

#include <libzfs.h>

#include "zpool_util.h"
#include "zfs_comutil.h"
#include "zfeature_common.h"

#include "statcommon.h"

static int zpool_do_create(int, char **);
static int zpool_do_destroy(int, char **);

static int zpool_do_add(int, char **);
static int zpool_do_remove(int, char **);
static int zpool_do_labelclear(int, char **);

static int zpool_do_list(int, char **);
static int zpool_do_iostat(int, char **);
static int zpool_do_status(int, char **);

static int zpool_do_online(int, char **);
static int zpool_do_offline(int, char **);
static int zpool_do_clear(int, char **);
static int zpool_do_reopen(int, char **);

static int zpool_do_reguid(int, char **);

static int zpool_do_attach(int, char **);
static int zpool_do_detach(int, char **);
static int zpool_do_replace(int, char **);
static int zpool_do_split(int, char **);

static int zpool_do_scrub(int, char **);

static int zpool_do_import(int, char **);
static int zpool_do_export(int, char **);

static int zpool_do_upgrade(int, char **);

static int zpool_do_history(int, char **);
static int zpool_do_events(int, char **);

static int zpool_do_get(int, char **);
static int zpool_do_set(int, char **);

static int zpool_do_sync(int, char **);

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
	HELP_LABELCLEAR,
	HELP_LIST,
	HELP_OFFLINE,
	HELP_ONLINE,
	HELP_REPLACE,
	HELP_REMOVE,
	HELP_SCRUB,
	HELP_STATUS,
	HELP_UPGRADE,
	HELP_EVENTS,
	HELP_GET,
	HELP_SET,
	HELP_SPLIT,
	HELP_SYNC,
	HELP_REGUID,
	HELP_REOPEN
} zpool_help_t;


/*
 * Flags for stats to display with "zpool iostats"
 */
enum iostat_type {
	IOS_DEFAULT = 0,
	IOS_LATENCY = 1,
	IOS_QUEUES = 2,
	IOS_L_HISTO = 3,
	IOS_RQ_HISTO = 4,
	IOS_COUNT,	/* always last element */
};

/* iostat_type entries as bitmasks */
#define	IOS_DEFAULT_M	(1ULL << IOS_DEFAULT)
#define	IOS_LATENCY_M	(1ULL << IOS_LATENCY)
#define	IOS_QUEUES_M	(1ULL << IOS_QUEUES)
#define	IOS_L_HISTO_M	(1ULL << IOS_L_HISTO)
#define	IOS_RQ_HISTO_M	(1ULL << IOS_RQ_HISTO)

/* Mask of all the histo bits */
#define	IOS_ANYHISTO_M (IOS_L_HISTO_M | IOS_RQ_HISTO_M)

/*
 * Lookup table for iostat flags to nvlist names.  Basically a list
 * of all the nvlists a flag requires.  Also specifies the order in
 * which data gets printed in zpool iostat.
 */
static const char *vsx_type_to_nvlist[IOS_COUNT][11] = {
	[IOS_L_HISTO] = {
	    ZPOOL_CONFIG_VDEV_TOT_R_LAT_HISTO,
	    ZPOOL_CONFIG_VDEV_TOT_W_LAT_HISTO,
	    ZPOOL_CONFIG_VDEV_DISK_R_LAT_HISTO,
	    ZPOOL_CONFIG_VDEV_DISK_W_LAT_HISTO,
	    ZPOOL_CONFIG_VDEV_SYNC_R_LAT_HISTO,
	    ZPOOL_CONFIG_VDEV_SYNC_W_LAT_HISTO,
	    ZPOOL_CONFIG_VDEV_ASYNC_R_LAT_HISTO,
	    ZPOOL_CONFIG_VDEV_ASYNC_W_LAT_HISTO,
	    ZPOOL_CONFIG_VDEV_SCRUB_LAT_HISTO,
	    NULL},
	[IOS_LATENCY] = {
	    ZPOOL_CONFIG_VDEV_TOT_R_LAT_HISTO,
	    ZPOOL_CONFIG_VDEV_TOT_W_LAT_HISTO,
	    ZPOOL_CONFIG_VDEV_DISK_R_LAT_HISTO,
	    ZPOOL_CONFIG_VDEV_DISK_W_LAT_HISTO,
	    NULL},
	[IOS_QUEUES] = {
	    ZPOOL_CONFIG_VDEV_SYNC_R_ACTIVE_QUEUE,
	    ZPOOL_CONFIG_VDEV_SYNC_W_ACTIVE_QUEUE,
	    ZPOOL_CONFIG_VDEV_ASYNC_R_ACTIVE_QUEUE,
	    ZPOOL_CONFIG_VDEV_ASYNC_W_ACTIVE_QUEUE,
	    ZPOOL_CONFIG_VDEV_SCRUB_ACTIVE_QUEUE,
	    NULL},
	[IOS_RQ_HISTO] = {
	    ZPOOL_CONFIG_VDEV_SYNC_IND_R_HISTO,
	    ZPOOL_CONFIG_VDEV_SYNC_AGG_R_HISTO,
	    ZPOOL_CONFIG_VDEV_SYNC_IND_W_HISTO,
	    ZPOOL_CONFIG_VDEV_SYNC_AGG_W_HISTO,
	    ZPOOL_CONFIG_VDEV_ASYNC_IND_R_HISTO,
	    ZPOOL_CONFIG_VDEV_ASYNC_AGG_R_HISTO,
	    ZPOOL_CONFIG_VDEV_ASYNC_IND_W_HISTO,
	    ZPOOL_CONFIG_VDEV_ASYNC_AGG_W_HISTO,
	    ZPOOL_CONFIG_VDEV_IND_SCRUB_HISTO,
	    ZPOOL_CONFIG_VDEV_AGG_SCRUB_HISTO,
	    NULL},
};


/*
 * Given a cb->cb_flags with a histogram bit set, return the iostat_type.
 * Right now, only one histo bit is ever set at one time, so we can
 * just do a highbit64(a)
 */
#define	IOS_HISTO_IDX(a)	(highbit64(a & IOS_ANYHISTO_M) - 1)

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
	{ "labelclear",	zpool_do_labelclear,	HELP_LABELCLEAR		},
	{ NULL },
	{ "list",	zpool_do_list,		HELP_LIST		},
	{ "iostat",	zpool_do_iostat,	HELP_IOSTAT		},
	{ "status",	zpool_do_status,	HELP_STATUS		},
	{ NULL },
	{ "online",	zpool_do_online,	HELP_ONLINE		},
	{ "offline",	zpool_do_offline,	HELP_OFFLINE		},
	{ "clear",	zpool_do_clear,		HELP_CLEAR		},
	{ "reopen",	zpool_do_reopen,	HELP_REOPEN		},
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
	{ "reguid",	zpool_do_reguid,	HELP_REGUID		},
	{ NULL },
	{ "history",	zpool_do_history,	HELP_HISTORY		},
	{ "events",	zpool_do_events,	HELP_EVENTS		},
	{ NULL },
	{ "get",	zpool_do_get,		HELP_GET		},
	{ "set",	zpool_do_set,		HELP_SET		},
	{ "sync",	zpool_do_sync,		HELP_SYNC		},
};

#define	NCOMMAND	(ARRAY_SIZE(command_table))

static zpool_command_t *current_command;
static char history_str[HIS_MAX_RECORD_LEN];
static boolean_t log_history = B_TRUE;
static uint_t timestamp_fmt = NODATE;

static const char *
get_usage(zpool_help_t idx)
{
	switch (idx) {
	case HELP_ADD:
		return (gettext("\tadd [-fgLnP] [-o property=value] "
		    "<pool> <vdev> ...\n"));
	case HELP_ATTACH:
		return (gettext("\tattach [-f] [-o property=value] "
		    "<pool> <device> <new-device>\n"));
	case HELP_CLEAR:
		return (gettext("\tclear [-nF] <pool> [device]\n"));
	case HELP_CREATE:
		return (gettext("\tcreate [-fnd] [-o property=value] ... \n"
		    "\t    [-O file-system-property=value] ... \n"
		    "\t    [-m mountpoint] [-R root] <pool> <vdev> ...\n"));
	case HELP_DESTROY:
		return (gettext("\tdestroy [-f] <pool>\n"));
	case HELP_DETACH:
		return (gettext("\tdetach <pool> <device>\n"));
	case HELP_EXPORT:
		return (gettext("\texport [-af] <pool> ...\n"));
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
		return (gettext("\tiostat [[[-c [script1,script2,...]"
		    "[-lq]]|[-rw]] [-T d | u] [-ghHLpPvy]\n"
		    "\t    [[pool ...]|[pool vdev ...]|[vdev ...]]"
		    " [interval [count]]\n"));
	case HELP_LABELCLEAR:
		return (gettext("\tlabelclear [-f] <vdev>\n"));
	case HELP_LIST:
		return (gettext("\tlist [-gHLpPv] [-o property[,...]] "
		    "[-T d|u] [pool] ... [interval [count]]\n"));
	case HELP_OFFLINE:
		return (gettext("\toffline [-f] [-t] <pool> <device> ...\n"));
	case HELP_ONLINE:
		return (gettext("\tonline <pool> <device> ...\n"));
	case HELP_REPLACE:
		return (gettext("\treplace [-f] [-o property=value] "
		    "<pool> <device> [new-device]\n"));
	case HELP_REMOVE:
		return (gettext("\tremove <pool> <device> ...\n"));
	case HELP_REOPEN:
		return (gettext("\treopen <pool>\n"));
	case HELP_SCRUB:
		return (gettext("\tscrub [-s | -p] <pool> ...\n"));
	case HELP_STATUS:
		return (gettext("\tstatus [-c [script1,script2,...]] [-gLPvxD]"
		    "[-T d|u] [pool] ... [interval [count]]\n"));
	case HELP_UPGRADE:
		return (gettext("\tupgrade\n"
		    "\tupgrade -v\n"
		    "\tupgrade [-V version] <-a | pool ...>\n"));
	case HELP_EVENTS:
		return (gettext("\tevents [-vHfc]\n"));
	case HELP_GET:
		return (gettext("\tget [-Hp] [-o \"all\" | field[,...]] "
		    "<\"all\" | property[,...]> <pool> ...\n"));
	case HELP_SET:
		return (gettext("\tset <property=value> <pool> \n"));
	case HELP_SPLIT:
		return (gettext("\tsplit [-gLnP] [-R altroot] [-o mntopts]\n"
		    "\t    [-o property=value] <pool> <newpool> "
		    "[<device> ...]\n"));
	case HELP_REGUID:
		return (gettext("\treguid <pool>\n"));
	case HELP_SYNC:
		return (gettext("\tsync [pool] ...\n"));
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

		(void) fprintf(fp, "\t%-15s   ", "feature@...");
		(void) fprintf(fp, "YES   disabled | enabled | active\n");

		(void) fprintf(fp, gettext("\nThe feature@ properties must be "
		    "appended with a feature name.\nSee zpool-features(5).\n"));
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
    boolean_t print_logs, int name_flags)
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

		vname = zpool_vdev_name(g_zfs, zhp, child[c], name_flags);
		print_vdev_tree(zhp, vname, child[c], indent + 2,
		    B_FALSE, name_flags);
		free(vname);
	}
}

static boolean_t
prop_list_contains_feature(nvlist_t *proplist)
{
	nvpair_t *nvp;
	for (nvp = nvlist_next_nvpair(proplist, NULL); NULL != nvp;
	    nvp = nvlist_next_nvpair(proplist, nvp)) {
		if (zpool_prop_feature(nvpair_name(nvp)))
			return (B_TRUE);
	}
	return (B_FALSE);
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
		const char *vname = zpool_prop_to_name(ZPOOL_PROP_VERSION);

		if ((prop = zpool_name_to_prop(propname)) == ZPROP_INVAL &&
		    !zpool_prop_feature(propname)) {
			(void) fprintf(stderr, gettext("property '%s' is "
			    "not a valid pool property\n"), propname);
			return (2);
		}

		/*
		 * feature@ properties and version should not be specified
		 * at the same time.
		 */
		if ((prop == ZPROP_INVAL && zpool_prop_feature(propname) &&
		    nvlist_exists(proplist, vname)) ||
		    (prop == ZPOOL_PROP_VERSION &&
		    prop_list_contains_feature(proplist))) {
			(void) fprintf(stderr, gettext("'feature@' and "
			    "'version' properties cannot be specified "
			    "together\n"));
			return (2);
		}


		if (zpool_prop_feature(propname))
			normnm = propname;
		else
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
 * Set a default property pair (name, string-value) in a property nvlist
 */
static int
add_prop_list_default(const char *propname, char *propval, nvlist_t **props,
    boolean_t poolprop)
{
	char *pval;

	if (nvlist_lookup_string(*props, propname, &pval) == 0)
		return (0);

	return (add_prop_list(propname, propval, props, B_TRUE));
}

/*
 * zpool add [-fgLnP] [-o property=value] <pool> <vdev> ...
 *
 *	-f	Force addition of devices, even if they appear in use
 *	-g	Display guid for individual vdev name.
 *	-L	Follow links when resolving vdev path name.
 *	-n	Do not add the devices, but display the resulting layout if
 *		they were to be added.
 *	-o	Set property=value.
 *	-P	Display full path for vdev name.
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
	int name_flags = 0;
	int c;
	nvlist_t *nvroot;
	char *poolname;
	int ret;
	zpool_handle_t *zhp;
	nvlist_t *config;
	nvlist_t *props = NULL;
	char *propval;

	/* check options */
	while ((c = getopt(argc, argv, "fgLno:P")) != -1) {
		switch (c) {
		case 'f':
			force = B_TRUE;
			break;
		case 'g':
			name_flags |= VDEV_NAME_GUID;
			break;
		case 'L':
			name_flags |= VDEV_NAME_FOLLOW_LINKS;
			break;
		case 'n':
			dryrun = B_TRUE;
			break;
		case 'o':
			if ((propval = strchr(optarg, '=')) == NULL) {
				(void) fprintf(stderr, gettext("missing "
				    "'=' for -o option\n"));
				usage(B_FALSE);
			}
			*propval = '\0';
			propval++;

			if ((strcmp(optarg, ZPOOL_CONFIG_ASHIFT) != 0) ||
			    (add_prop_list(optarg, propval, &props, B_TRUE)))
				usage(B_FALSE);
			break;
		case 'P':
			name_flags |= VDEV_NAME_PATH;
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

	/* unless manually specified use "ashift" pool property (if set) */
	if (!nvlist_exists(props, ZPOOL_CONFIG_ASHIFT)) {
		int intval;
		zprop_source_t src;
		char strval[ZPOOL_MAXPROPLEN];

		intval = zpool_get_prop_int(zhp, ZPOOL_PROP_ASHIFT, &src);
		if (src != ZPROP_SRC_DEFAULT) {
			(void) sprintf(strval, "%" PRId32, intval);
			verify(add_prop_list(ZPOOL_CONFIG_ASHIFT, strval,
			    &props, B_TRUE) == 0);
		}
	}

	/* pass off to get_vdev_spec for processing */
	nvroot = make_root_vdev(zhp, props, force, !force, B_FALSE, dryrun,
	    argc, argv);
	if (nvroot == NULL) {
		zpool_close(zhp);
		return (1);
	}

	if (dryrun) {
		nvlist_t *poolnvroot;
		nvlist_t **l2child;
		uint_t l2children, c;
		char *vname;
		boolean_t hadcache = B_FALSE;

		verify(nvlist_lookup_nvlist(config, ZPOOL_CONFIG_VDEV_TREE,
		    &poolnvroot) == 0);

		(void) printf(gettext("would update '%s' to the following "
		    "configuration:\n"), zpool_get_name(zhp));

		/* print original main pool and new tree */
		print_vdev_tree(zhp, poolname, poolnvroot, 0, B_FALSE,
		    name_flags);
		print_vdev_tree(zhp, NULL, nvroot, 0, B_FALSE, name_flags);

		/* Do the same for the logs */
		if (num_logs(poolnvroot) > 0) {
			print_vdev_tree(zhp, "logs", poolnvroot, 0, B_TRUE,
			    name_flags);
			print_vdev_tree(zhp, NULL, nvroot, 0, B_TRUE,
			    name_flags);
		} else if (num_logs(nvroot) > 0) {
			print_vdev_tree(zhp, "logs", nvroot, 0, B_TRUE,
			    name_flags);
		}

		/* Do the same for the caches */
		if (nvlist_lookup_nvlist_array(poolnvroot, ZPOOL_CONFIG_L2CACHE,
		    &l2child, &l2children) == 0 && l2children) {
			hadcache = B_TRUE;
			(void) printf(gettext("\tcache\n"));
			for (c = 0; c < l2children; c++) {
				vname = zpool_vdev_name(g_zfs, NULL,
				    l2child[c], name_flags);
				(void) printf("\t  %s\n", vname);
				free(vname);
			}
		}
		if (nvlist_lookup_nvlist_array(nvroot, ZPOOL_CONFIG_L2CACHE,
		    &l2child, &l2children) == 0 && l2children) {
			if (!hadcache)
				(void) printf(gettext("\tcache\n"));
			for (c = 0; c < l2children; c++) {
				vname = zpool_vdev_name(g_zfs, NULL,
				    l2child[c], name_flags);
				(void) printf("\t  %s\n", vname);
				free(vname);
			}
		}

		ret = 0;
	} else {
		ret = (zpool_add(zhp, nvroot) != 0);
	}

	nvlist_free(props);
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
	zpool_handle_t *zhp = NULL;

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
	zpool_close(zhp);

	return (ret);
}

/*
 * zpool labelclear [-f] <vdev>
 *
 *	-f	Force clearing the label for the vdevs which are members of
 *		the exported or foreign pools.
 *
 * Verifies that the vdev is not active and zeros out the label information
 * on the device.
 */
int
zpool_do_labelclear(int argc, char **argv)
{
	char vdev[MAXPATHLEN];
	char *name = NULL;
	struct stat st;
	int c, fd = -1, ret = 0;
	nvlist_t *config;
	pool_state_t state;
	boolean_t inuse = B_FALSE;
	boolean_t force = B_FALSE;

	/* check options */
	while ((c = getopt(argc, argv, "f")) != -1) {
		switch (c) {
		case 'f':
			force = B_TRUE;
			break;
		default:
			(void) fprintf(stderr, gettext("invalid option '%c'\n"),
			    optopt);
			usage(B_FALSE);
		}
	}

	argc -= optind;
	argv += optind;

	/* get vdev name */
	if (argc < 1) {
		(void) fprintf(stderr, gettext("missing vdev name\n"));
		usage(B_FALSE);
	}
	if (argc > 1) {
		(void) fprintf(stderr, gettext("too many arguments\n"));
		usage(B_FALSE);
	}

	/*
	 * Check if we were given absolute path and use it as is.
	 * Otherwise if the provided vdev name doesn't point to a file,
	 * try prepending expected disk paths and partition numbers.
	 */
	(void) strlcpy(vdev, argv[0], sizeof (vdev));
	if (vdev[0] != '/' && stat(vdev, &st) != 0) {
		int error;

		error = zfs_resolve_shortname(argv[0], vdev, MAXPATHLEN);
		if (error == 0 && zfs_dev_is_whole_disk(vdev)) {
			if (zfs_append_partition(vdev, MAXPATHLEN) == -1)
				error = ENOENT;
		}

		if (error || (stat(vdev, &st) != 0)) {
			(void) fprintf(stderr, gettext(
			    "failed to find device %s, try specifying absolute "
			    "path instead\n"), argv[0]);
			return (1);
		}
	}

	if ((fd = open(vdev, O_RDWR)) < 0) {
		(void) fprintf(stderr, gettext("failed to open %s: %s\n"),
		    vdev, strerror(errno));
		return (1);
	}

	if (ioctl(fd, BLKFLSBUF) != 0)
		(void) fprintf(stderr, gettext("failed to invalidate "
		    "cache for %s: %s\n"), vdev, strerror(errno));

	if (zpool_read_label(fd, &config, NULL) != 0 || config == NULL) {
		(void) fprintf(stderr,
		    gettext("failed to check state for %s\n"), vdev);
		return (1);
	}
	nvlist_free(config);

	ret = zpool_in_use(g_zfs, fd, &state, &name, &inuse);
	if (ret != 0) {
		(void) fprintf(stderr,
		    gettext("failed to check state for %s\n"), vdev);
		return (1);
	}

	if (!inuse)
		goto wipe_label;

	switch (state) {
	default:
	case POOL_STATE_ACTIVE:
	case POOL_STATE_SPARE:
	case POOL_STATE_L2CACHE:
		(void) fprintf(stderr, gettext(
		    "%s is a member (%s) of pool \"%s\"\n"),
		    vdev, zpool_pool_state_to_name(state), name);
		ret = 1;
		goto errout;

	case POOL_STATE_EXPORTED:
		if (force)
			break;
		(void) fprintf(stderr, gettext(
		    "use '-f' to override the following error:\n"
		    "%s is a member of exported pool \"%s\"\n"),
		    vdev, name);
		ret = 1;
		goto errout;

	case POOL_STATE_POTENTIALLY_ACTIVE:
		if (force)
			break;
		(void) fprintf(stderr, gettext(
		    "use '-f' to override the following error:\n"
		    "%s is a member of potentially active pool \"%s\"\n"),
		    vdev, name);
		ret = 1;
		goto errout;

	case POOL_STATE_DESTROYED:
		/* inuse should never be set for a destroyed pool */
		assert(0);
		break;
	}

wipe_label:
	ret = zpool_clear_label(fd);
	if (ret != 0) {
		(void) fprintf(stderr,
		    gettext("failed to clear label for %s\n"), vdev);
	}

errout:
	free(name);
	(void) close(fd);

	return (ret);
}

/*
 * zpool create [-fnd] [-o property=value] ...
 *		[-O file-system-property=value] ...
 *		[-R root] [-m mountpoint] <pool> <dev> ...
 *
 *	-f	Force creation, even if devices appear in use
 *	-n	Do not create the pool, but display the resulting layout if it
 *		were to be created.
 *      -R	Create a pool under an alternate root
 *      -m	Set default mountpoint for the root dataset.  By default it's
 *		'/<pool>'
 *	-o	Set property=value.
 *	-o	Set feature@feature=enabled|disabled.
 *	-d	Don't automatically enable all supported pool features
 *		(individual features can be enabled with -o).
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
	boolean_t enable_all_pool_feat = B_TRUE;
	int c;
	nvlist_t *nvroot = NULL;
	char *poolname;
	char *tname = NULL;
	int ret = 1;
	char *altroot = NULL;
	char *mountpoint = NULL;
	nvlist_t *fsprops = NULL;
	nvlist_t *props = NULL;
	char *propval;

	/* check options */
	while ((c = getopt(argc, argv, ":fndR:m:o:O:t:")) != -1) {
		switch (c) {
		case 'f':
			force = B_TRUE;
			break;
		case 'n':
			dryrun = B_TRUE;
			break;
		case 'd':
			enable_all_pool_feat = B_FALSE;
			break;
		case 'R':
			altroot = optarg;
			if (add_prop_list(zpool_prop_to_name(
			    ZPOOL_PROP_ALTROOT), optarg, &props, B_TRUE))
				goto errout;
			if (add_prop_list_default(zpool_prop_to_name(
			    ZPOOL_PROP_CACHEFILE), "none", &props, B_TRUE))
				goto errout;
			break;
		case 'm':
			/* Equivalent to -O mountpoint=optarg */
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

			/*
			 * If the user is creating a pool that doesn't support
			 * feature flags, don't enable any features.
			 */
			if (zpool_name_to_prop(optarg) == ZPOOL_PROP_VERSION) {
				char *end;
				u_longlong_t ver;

				ver = strtoull(propval, &end, 10);
				if (*end == '\0' &&
				    ver < SPA_VERSION_FEATURES) {
					enable_all_pool_feat = B_FALSE;
				}
			}
			if (zpool_name_to_prop(optarg) == ZPOOL_PROP_ALTROOT)
				altroot = propval;
			break;
		case 'O':
			if ((propval = strchr(optarg, '=')) == NULL) {
				(void) fprintf(stderr, gettext("missing "
				    "'=' for -O option\n"));
				goto errout;
			}
			*propval = '\0';
			propval++;

			/*
			 * Mountpoints are checked and then added later.
			 * Uniquely among properties, they can be specified
			 * more than once, to avoid conflict with -m.
			 */
			if (0 == strcmp(optarg,
			    zfs_prop_to_name(ZFS_PROP_MOUNTPOINT))) {
				mountpoint = propval;
			} else if (add_prop_list(optarg, propval, &fsprops,
			    B_FALSE)) {
				goto errout;
			}
			break;
		case 't':
			/*
			 * Sanity check temporary pool name.
			 */
			if (strchr(optarg, '/') != NULL) {
				(void) fprintf(stderr, gettext("cannot create "
				    "'%s': invalid character '/' in temporary "
				    "name\n"), optarg);
				(void) fprintf(stderr, gettext("use 'zfs "
				    "create' to create a dataset\n"));
				goto errout;
			}

			if (add_prop_list(zpool_prop_to_name(
			    ZPOOL_PROP_TNAME), optarg, &props, B_TRUE))
				goto errout;
			if (add_prop_list_default(zpool_prop_to_name(
			    ZPOOL_PROP_CACHEFILE), "none", &props, B_TRUE))
				goto errout;
			tname = optarg;
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
	nvroot = make_root_vdev(NULL, props, force, !force, B_FALSE, dryrun,
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

	/*
	 * Now that the mountpoint's validity has been checked, ensure that
	 * the property is set appropriately prior to creating the pool.
	 */
	if (mountpoint != NULL) {
		ret = add_prop_list(zfs_prop_to_name(ZFS_PROP_MOUNTPOINT),
		    mountpoint, &fsprops, B_FALSE);
		if (ret != 0)
			goto errout;
	}

	ret = 1;
	if (dryrun) {
		/*
		 * For a dry run invocation, print out a basic message and run
		 * through all the vdevs in the list and print out in an
		 * appropriate hierarchy.
		 */
		(void) printf(gettext("would create '%s' with the "
		    "following layout:\n\n"), poolname);

		print_vdev_tree(NULL, poolname, nvroot, 0, B_FALSE, 0);
		if (num_logs(nvroot) > 0)
			print_vdev_tree(NULL, "logs", nvroot, 0, B_TRUE, 0);

		ret = 0;
	} else {
		/*
		 * Hand off to libzfs.
		 */
		spa_feature_t i;
		for (i = 0; i < SPA_FEATURES; i++) {
			char propname[MAXPATHLEN];
			char *propval;
			zfeature_info_t *feat = &spa_feature_table[i];

			(void) snprintf(propname, sizeof (propname),
			    "feature@%s", feat->fi_uname);

			/*
			 * Only features contained in props will be enabled:
			 * remove from the nvlist every ZFS_FEATURE_DISABLED
			 * value and add every missing ZFS_FEATURE_ENABLED if
			 * enable_all_pool_feat is set.
			 */
			if (!nvlist_lookup_string(props, propname, &propval)) {
				if (strcmp(propval, ZFS_FEATURE_DISABLED) == 0)
					(void) nvlist_remove_all(props,
					    propname);
			} else if (enable_all_pool_feat) {
				ret = add_prop_list(propname,
				    ZFS_FEATURE_ENABLED, &props, B_TRUE);
				if (ret != 0)
					goto errout;
			}
		}

		ret = 1;
		if (zpool_create(g_zfs, poolname,
		    nvroot, props, fsprops) == 0) {
			zfs_handle_t *pool = zfs_open(g_zfs,
			    tname ? tname : poolname, ZFS_TYPE_FILESYSTEM);
			if (pool != NULL) {
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
		zpool_close(zhp);
		return (1);
	}

	/* The history must be logged as part of the export */
	log_history = B_FALSE;

	ret = (zpool_destroy(zhp, history_str) != 0);

	zpool_close(zhp);

	return (ret);
}

typedef struct export_cbdata {
	boolean_t force;
	boolean_t hardforce;
} export_cbdata_t;

/*
 * Export one pool
 */
int
zpool_export_one(zpool_handle_t *zhp, void *data)
{
	export_cbdata_t *cb = data;

	if (zpool_disable_datasets(zhp, cb->force) != 0)
		return (1);

	/* The history must be logged as part of the export */
	log_history = B_FALSE;

	if (cb->hardforce) {
		if (zpool_export_force(zhp, history_str) != 0)
			return (1);
	} else if (zpool_export(zhp, cb->force, history_str) != 0) {
		return (1);
	}

	return (0);
}

/*
 * zpool export [-f] <pool> ...
 *
 *	-a	Export all pools
 *	-f	Forcefully unmount datasets
 *
 * Export the given pools.  By default, the command will attempt to cleanly
 * unmount any active datasets within the pool.  If the '-f' flag is specified,
 * then the datasets will be forcefully unmounted.
 */
int
zpool_do_export(int argc, char **argv)
{
	export_cbdata_t cb;
	boolean_t do_all = B_FALSE;
	boolean_t force = B_FALSE;
	boolean_t hardforce = B_FALSE;
	int c, ret;

	/* check options */
	while ((c = getopt(argc, argv, "afF")) != -1) {
		switch (c) {
		case 'a':
			do_all = B_TRUE;
			break;
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

	cb.force = force;
	cb.hardforce = hardforce;
	argc -= optind;
	argv += optind;

	if (do_all) {
		if (argc != 0) {
			(void) fprintf(stderr, gettext("too many arguments\n"));
			usage(B_FALSE);
		}

		return (for_each_pool(argc, argv, B_TRUE, NULL,
		    zpool_export_one, &cb));
	}

	/* check arguments */
	if (argc < 1) {
		(void) fprintf(stderr, gettext("missing pool argument\n"));
		usage(B_FALSE);
	}

	ret = for_each_pool(argc, argv, B_TRUE, NULL, zpool_export_one, &cb);

	return (ret);
}

/*
 * Given a vdev configuration, determine the maximum width needed for the device
 * name column.
 */
static int
max_width(zpool_handle_t *zhp, nvlist_t *nv, int depth, int max,
    int name_flags)
{
	char *name;
	nvlist_t **child;
	uint_t c, children;
	int ret;

	name = zpool_vdev_name(g_zfs, zhp, nv, name_flags);
	if (strlen(name) + depth > max)
		max = strlen(name) + depth;

	free(name);

	if (nvlist_lookup_nvlist_array(nv, ZPOOL_CONFIG_SPARES,
	    &child, &children) == 0) {
		for (c = 0; c < children; c++)
			if ((ret = max_width(zhp, child[c], depth + 2,
			    max, name_flags)) > max)
				max = ret;
	}

	if (nvlist_lookup_nvlist_array(nv, ZPOOL_CONFIG_L2CACHE,
	    &child, &children) == 0) {
		for (c = 0; c < children; c++)
			if ((ret = max_width(zhp, child[c], depth + 2,
			    max, name_flags)) > max)
				max = ret;
	}

	if (nvlist_lookup_nvlist_array(nv, ZPOOL_CONFIG_CHILDREN,
	    &child, &children) == 0) {
		for (c = 0; c < children; c++)
			if ((ret = max_width(zhp, child[c], depth + 2,
			    max, name_flags)) > max)
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

typedef struct status_cbdata {
	int		cb_count;
	int		cb_name_flags;
	int		cb_namewidth;
	boolean_t	cb_allpools;
	boolean_t	cb_verbose;
	boolean_t	cb_explain;
	boolean_t	cb_first;
	boolean_t	cb_dedup_stats;
	boolean_t	cb_print_status;
	vdev_cmd_data_list_t	*vcdl;
} status_cbdata_t;

/* Return 1 if string is NULL, empty, or whitespace; return 0 otherwise. */
static int
is_blank_str(char *str)
{
	while (str != NULL && *str != '\0') {
		if (!isblank(*str))
			return (0);
		str++;
	}
	return (1);
}

/* Print command output lines for specific vdev in a specific pool */
static void
zpool_print_cmd(vdev_cmd_data_list_t *vcdl, const char *pool, char *path)
{
	vdev_cmd_data_t *data;
	int i, j;
	char *val;

	for (i = 0; i < vcdl->count; i++) {
		if ((strcmp(vcdl->data[i].path, path) != 0) ||
		    (strcmp(vcdl->data[i].pool, pool) != 0)) {
			/* Not the vdev we're looking for */
			continue;
		}

		data = &vcdl->data[i];
		/* Print out all the output values for this vdev */
		for (j = 0; j < vcdl->uniq_cols_cnt; j++) {
			val = NULL;
			/* Does this vdev have values for this column? */
			for (int k = 0; k < data->cols_cnt; k++) {
				if (strcmp(data->cols[k],
				    vcdl->uniq_cols[j]) == 0) {
					/* yes it does, record the value */
					val = data->lines[k];
					break;
				}
			}
			/*
			 * Mark empty values with dashes to make output
			 * awk-able.
			 */
			if (is_blank_str(val))
				val = "-";

			printf("%*s", vcdl->uniq_cols_width[j], val);
			if (j < vcdl->uniq_cols_cnt - 1)
				printf("  ");
		}

		/* Print out any values that aren't in a column at the end */
		for (j = data->cols_cnt; j < data->lines_cnt; j++) {
			/* Did we have any columns?  If so print a spacer. */
			if (vcdl->uniq_cols_cnt > 0)
				printf("  ");

			val = data->lines[j];
			printf("%s", val ? val : "");
		}
		break;
	}
}

/*
 * Print out configuration state as requested by status_callback.
 */
static void
print_status_config(zpool_handle_t *zhp, status_cbdata_t *cb, const char *name,
    nvlist_t *nv, int depth, boolean_t isspare)
{
	nvlist_t **child;
	uint_t c, children;
	pool_scan_stat_t *ps = NULL;
	vdev_stat_t *vs;
	char rbuf[6], wbuf[6], cbuf[6];
	char *vname;
	uint64_t notpresent;
	spare_cbdata_t spare_cb;
	char *state;
	char *path = NULL;

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

	(void) printf("\t%*s%-*s  %-8s", depth, "", cb->cb_namewidth - depth,
	    name, state);

	if (!isspare) {
		zfs_nicenum(vs->vs_read_errors, rbuf, sizeof (rbuf));
		zfs_nicenum(vs->vs_write_errors, wbuf, sizeof (wbuf));
		zfs_nicenum(vs->vs_checksum_errors, cbuf, sizeof (cbuf));
		(void) printf(" %5s %5s %5s", rbuf, wbuf, cbuf);
	}

	if (nvlist_lookup_uint64(nv, ZPOOL_CONFIG_NOT_PRESENT,
	    &notpresent) == 0) {
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

		case VDEV_AUX_UNSUP_FEAT:
			(void) printf(gettext("unsupported feature(s)"));
			break;

		case VDEV_AUX_SPARED:
			verify(nvlist_lookup_uint64(nv, ZPOOL_CONFIG_GUID,
			    &spare_cb.cb_guid) == 0);
			if (zpool_iter(g_zfs, find_spare, &spare_cb) == 1) {
				if (strcmp(zpool_get_name(spare_cb.cb_zhp),
				    zpool_get_name(zhp)) == 0)
					(void) printf(gettext("currently in "
					    "use"));
				else
					(void) printf(gettext("in use by "
					    "pool '%s'"),
					    zpool_get_name(spare_cb.cb_zhp));
				zpool_close(spare_cb.cb_zhp);
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

		case VDEV_AUX_ACTIVE:
			(void) printf(gettext("currently in use"));
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

	if (cb->vcdl != NULL) {
		if (nvlist_lookup_string(nv, ZPOOL_CONFIG_PATH, &path) == 0) {
			printf("  ");
			zpool_print_cmd(cb->vcdl, zpool_get_name(zhp), path);
		}
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
		vname = zpool_vdev_name(g_zfs, zhp, child[c],
		    cb->cb_name_flags | VDEV_NAME_TYPE_ID);
		print_status_config(zhp, cb, vname, child[c], depth + 2,
		    isspare);
		free(vname);
	}
}

/*
 * Print the configuration of an exported pool.  Iterate over all vdevs in the
 * pool, printing out the name and status for each one.
 */
static void
print_import_config(status_cbdata_t *cb, const char *name, nvlist_t *nv,
    int depth)
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

	(void) printf("\t%*s%-*s", depth, "", cb->cb_namewidth - depth, name);
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

		case VDEV_AUX_UNSUP_FEAT:
			(void) printf(gettext("unsupported feature(s)"));
			break;

		case VDEV_AUX_ERR_EXCEEDED:
			(void) printf(gettext("too many errors"));
			break;

		case VDEV_AUX_ACTIVE:
			(void) printf(gettext("currently in use"));
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

		vname = zpool_vdev_name(g_zfs, NULL, child[c],
		    cb->cb_name_flags | VDEV_NAME_TYPE_ID);
		print_import_config(cb, vname, child[c], depth + 2);
		free(vname);
	}

	if (nvlist_lookup_nvlist_array(nv, ZPOOL_CONFIG_L2CACHE,
	    &child, &children) == 0) {
		(void) printf(gettext("\tcache\n"));
		for (c = 0; c < children; c++) {
			vname = zpool_vdev_name(g_zfs, NULL, child[c],
			    cb->cb_name_flags);
			(void) printf("\t  %s\n", vname);
			free(vname);
		}
	}

	if (nvlist_lookup_nvlist_array(nv, ZPOOL_CONFIG_SPARES,
	    &child, &children) == 0) {
		(void) printf(gettext("\tspares\n"));
		for (c = 0; c < children; c++) {
			vname = zpool_vdev_name(g_zfs, NULL, child[c],
			    cb->cb_name_flags);
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
print_logs(zpool_handle_t *zhp, status_cbdata_t *cb, nvlist_t *nv)
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
		name = zpool_vdev_name(g_zfs, zhp, child[c],
		    cb->cb_name_flags | VDEV_NAME_TYPE_ID);
		if (cb->cb_print_status)
			print_status_config(zhp, cb, name, child[c], 2,
			    B_FALSE);
		else
			print_import_config(cb, name, child[c], 2);
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
	uint64_t hostid = 0;
	char *msgid;
	char *hostname = "unknown";
	nvlist_t *nvroot, *nvinfo;
	zpool_status_t reason;
	zpool_errata_t errata;
	const char *health;
	uint_t vsc;
	char *comment;
	status_cbdata_t cb = { 0 };

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

	reason = zpool_import_status(config, &msgid, &errata);

	(void) printf(gettext("   pool: %s\n"), name);
	(void) printf(gettext("     id: %llu\n"), (u_longlong_t)guid);
	(void) printf(gettext("  state: %s"), health);
	if (pool_state == POOL_STATE_DESTROYED)
		(void) printf(gettext(" (DESTROYED)"));
	(void) printf("\n");

	switch (reason) {
	case ZPOOL_STATUS_MISSING_DEV_R:
	case ZPOOL_STATUS_MISSING_DEV_NR:
	case ZPOOL_STATUS_BAD_GUID_SUM:
		(void) printf(gettext(" status: One or more devices are "
		    "missing from the system.\n"));
		break;

	case ZPOOL_STATUS_CORRUPT_LABEL_R:
	case ZPOOL_STATUS_CORRUPT_LABEL_NR:
		(void) printf(gettext(" status: One or more devices contains "
		    "corrupted data.\n"));
		break;

	case ZPOOL_STATUS_CORRUPT_DATA:
		(void) printf(
		    gettext(" status: The pool data is corrupted.\n"));
		break;

	case ZPOOL_STATUS_OFFLINE_DEV:
		(void) printf(gettext(" status: One or more devices "
		    "are offlined.\n"));
		break;

	case ZPOOL_STATUS_CORRUPT_POOL:
		(void) printf(gettext(" status: The pool metadata is "
		    "corrupted.\n"));
		break;

	case ZPOOL_STATUS_VERSION_OLDER:
		(void) printf(gettext(" status: The pool is formatted using a "
		    "legacy on-disk version.\n"));
		break;

	case ZPOOL_STATUS_VERSION_NEWER:
		(void) printf(gettext(" status: The pool is formatted using an "
		    "incompatible version.\n"));
		break;

	case ZPOOL_STATUS_FEAT_DISABLED:
		(void) printf(gettext(" status: Some supported features are "
		    "not enabled on the pool.\n"));
		break;

	case ZPOOL_STATUS_UNSUP_FEAT_READ:
		(void) printf(gettext("status: The pool uses the following "
		    "feature(s) not supported on this system:\n"));
		zpool_print_unsup_feat(config);
		break;

	case ZPOOL_STATUS_UNSUP_FEAT_WRITE:
		(void) printf(gettext("status: The pool can only be accessed "
		    "in read-only mode on this system. It\n\tcannot be "
		    "accessed in read-write mode because it uses the "
		    "following\n\tfeature(s) not supported on this system:\n"));
		zpool_print_unsup_feat(config);
		break;

	case ZPOOL_STATUS_HOSTID_ACTIVE:
		(void) printf(gettext(" status: The pool is currently "
		    "imported by another system.\n"));
		break;

	case ZPOOL_STATUS_HOSTID_REQUIRED:
		(void) printf(gettext(" status: The pool has the "
		    "multihost property on.  It cannot\n\tbe safely imported "
		    "when the system hostid is not set.\n"));
		break;

	case ZPOOL_STATUS_HOSTID_MISMATCH:
		(void) printf(gettext(" status: The pool was last accessed by "
		    "another system.\n"));
		break;

	case ZPOOL_STATUS_FAULTED_DEV_R:
	case ZPOOL_STATUS_FAULTED_DEV_NR:
		(void) printf(gettext(" status: One or more devices are "
		    "faulted.\n"));
		break;

	case ZPOOL_STATUS_BAD_LOG:
		(void) printf(gettext(" status: An intent log record cannot be "
		    "read.\n"));
		break;

	case ZPOOL_STATUS_RESILVERING:
		(void) printf(gettext(" status: One or more devices were being "
		    "resilvered.\n"));
		break;

	case ZPOOL_STATUS_ERRATA:
		(void) printf(gettext(" status: Errata #%d detected.\n"),
		    errata);
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
		if (reason == ZPOOL_STATUS_VERSION_OLDER ||
		    reason == ZPOOL_STATUS_FEAT_DISABLED) {
			(void) printf(gettext(" action: The pool can be "
			    "imported using its name or numeric identifier, "
			    "though\n\tsome features will not be available "
			    "without an explicit 'zpool upgrade'.\n"));
		} else if (reason == ZPOOL_STATUS_HOSTID_MISMATCH) {
			(void) printf(gettext(" action: The pool can be "
			    "imported using its name or numeric "
			    "identifier and\n\tthe '-f' flag.\n"));
		} else if (reason == ZPOOL_STATUS_ERRATA) {
			switch (errata) {
			case ZPOOL_ERRATA_NONE:
				break;

			case ZPOOL_ERRATA_ZOL_2094_SCRUB:
				(void) printf(gettext(" action: The pool can "
				    "be imported using its name or numeric "
				    "identifier,\n\thowever there is a compat"
				    "ibility issue which should be corrected"
				    "\n\tby running 'zpool scrub'\n"));
				break;

			case ZPOOL_ERRATA_ZOL_2094_ASYNC_DESTROY:
				(void) printf(gettext(" action: The pool can"
				    "not be imported with this version of ZFS "
				    "due to\n\tan active asynchronous destroy. "
				    "Revert to an earlier version\n\tand "
				    "allow the destroy to complete before "
				    "updating.\n"));
				break;

			default:
				/*
				 * All errata must contain an action message.
				 */
				assert(0);
			}
		} else {
			(void) printf(gettext(" action: The pool can be "
			    "imported using its name or numeric "
			    "identifier.\n"));
		}
	} else if (vs->vs_state == VDEV_STATE_DEGRADED) {
		(void) printf(gettext(" action: The pool can be imported "
		    "despite missing or damaged devices.  The\n\tfault "
		    "tolerance of the pool may be compromised if imported.\n"));
	} else {
		switch (reason) {
		case ZPOOL_STATUS_VERSION_NEWER:
			(void) printf(gettext(" action: The pool cannot be "
			    "imported.  Access the pool on a system running "
			    "newer\n\tsoftware, or recreate the pool from "
			    "backup.\n"));
			break;
		case ZPOOL_STATUS_UNSUP_FEAT_READ:
			(void) printf(gettext("action: The pool cannot be "
			    "imported. Access the pool on a system that "
			    "supports\n\tthe required feature(s), or recreate "
			    "the pool from backup.\n"));
			break;
		case ZPOOL_STATUS_UNSUP_FEAT_WRITE:
			(void) printf(gettext("action: The pool cannot be "
			    "imported in read-write mode. Import the pool "
			    "with\n"
			    "\t\"-o readonly=on\", access the pool on a system "
			    "that supports the\n\trequired feature(s), or "
			    "recreate the pool from backup.\n"));
			break;
		case ZPOOL_STATUS_MISSING_DEV_R:
		case ZPOOL_STATUS_MISSING_DEV_NR:
		case ZPOOL_STATUS_BAD_GUID_SUM:
			(void) printf(gettext(" action: The pool cannot be "
			    "imported. Attach the missing\n\tdevices and try "
			    "again.\n"));
			break;
		case ZPOOL_STATUS_HOSTID_ACTIVE:
			VERIFY0(nvlist_lookup_nvlist(config,
			    ZPOOL_CONFIG_LOAD_INFO, &nvinfo));

			if (nvlist_exists(nvinfo, ZPOOL_CONFIG_MMP_HOSTNAME))
				hostname = fnvlist_lookup_string(nvinfo,
				    ZPOOL_CONFIG_MMP_HOSTNAME);

			if (nvlist_exists(nvinfo, ZPOOL_CONFIG_MMP_HOSTID))
				hostid = fnvlist_lookup_uint64(nvinfo,
				    ZPOOL_CONFIG_MMP_HOSTID);

			(void) printf(gettext(" action: The pool must be "
			    "exported from %s (hostid=%lx)\n\tbefore it "
			    "can be safely imported.\n"), hostname,
			    (unsigned long) hostid);
			break;
		case ZPOOL_STATUS_HOSTID_REQUIRED:
			(void) printf(gettext(" action: Set a unique system "
			    "hostid with the zgenhostid(8) command.\n"));
			break;
		default:
			(void) printf(gettext(" action: The pool cannot be "
			    "imported due to damaged devices or data.\n"));
		}
	}

	/* Print the comment attached to the pool. */
	if (nvlist_lookup_string(config, ZPOOL_CONFIG_COMMENT, &comment) == 0)
		(void) printf(gettext("comment: %s\n"), comment);

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
		(void) printf(gettext("   see: http://zfsonlinux.org/msg/%s\n"),
		    msgid);

	(void) printf(gettext(" config:\n\n"));

	cb.cb_namewidth = max_width(NULL, nvroot, 0, strlen(name),
	    VDEV_NAME_TYPE_ID);
	if (cb.cb_namewidth < 10)
		cb.cb_namewidth = 10;

	print_import_config(&cb, name, nvroot, 0);
	if (num_logs(nvroot) > 0)
		print_logs(NULL, &cb, nvroot);

	if (reason == ZPOOL_STATUS_BAD_GUID_SUM) {
		(void) printf(gettext("\n\tAdditional devices are known to "
		    "be part of this pool, though their\n\texact "
		    "configuration cannot be determined.\n"));
	}
}

static boolean_t
zfs_force_import_required(nvlist_t *config)
{
	uint64_t state;
	uint64_t hostid = 0;
	nvlist_t *nvinfo;

	state = fnvlist_lookup_uint64(config, ZPOOL_CONFIG_POOL_STATE);
	(void) nvlist_lookup_uint64(config, ZPOOL_CONFIG_HOSTID, &hostid);

	if (state != POOL_STATE_EXPORTED && hostid != get_system_hostid())
		return (B_TRUE);

	nvinfo = fnvlist_lookup_nvlist(config, ZPOOL_CONFIG_LOAD_INFO);
	if (nvlist_exists(nvinfo, ZPOOL_CONFIG_MMP_STATE)) {
		mmp_state_t mmp_state = fnvlist_lookup_uint64(nvinfo,
		    ZPOOL_CONFIG_MMP_STATE);

		if (mmp_state != MMP_STATE_INACTIVE)
			return (B_TRUE);
	}

	return (B_FALSE);
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

	name = fnvlist_lookup_string(config, ZPOOL_CONFIG_POOL_NAME);
	state = fnvlist_lookup_uint64(config, ZPOOL_CONFIG_POOL_STATE);
	version = fnvlist_lookup_uint64(config, ZPOOL_CONFIG_VERSION);

	if (!SPA_VERSION_IS_SUPPORTED(version)) {
		(void) fprintf(stderr, gettext("cannot import '%s': pool "
		    "is formatted using an unsupported ZFS version\n"), name);
		return (1);
	} else if (zfs_force_import_required(config) &&
	    !(flags & ZFS_IMPORT_ANY_HOST)) {
		mmp_state_t mmp_state = MMP_STATE_INACTIVE;
		nvlist_t *nvinfo;

		nvinfo = fnvlist_lookup_nvlist(config, ZPOOL_CONFIG_LOAD_INFO);
		if (nvlist_exists(nvinfo, ZPOOL_CONFIG_MMP_STATE))
			mmp_state = fnvlist_lookup_uint64(nvinfo,
			    ZPOOL_CONFIG_MMP_STATE);

		if (mmp_state == MMP_STATE_ACTIVE) {
			char *hostname = "<unknown>";
			uint64_t hostid = 0;

			if (nvlist_exists(nvinfo, ZPOOL_CONFIG_MMP_HOSTNAME))
				hostname = fnvlist_lookup_string(nvinfo,
				    ZPOOL_CONFIG_MMP_HOSTNAME);

			if (nvlist_exists(nvinfo, ZPOOL_CONFIG_MMP_HOSTID))
				hostid = fnvlist_lookup_uint64(nvinfo,
				    ZPOOL_CONFIG_MMP_HOSTID);

			(void) fprintf(stderr, gettext("cannot import '%s': "
			    "pool is imported on %s (hostid: "
			    "0x%lx)\nExport the pool on the other system, "
			    "then run 'zpool import'.\n"),
			    name, hostname, (unsigned long) hostid);
		} else if (mmp_state == MMP_STATE_NO_HOSTID) {
			(void) fprintf(stderr, gettext("Cannot import '%s': "
			    "pool has the multihost property on and the\n"
			    "system's hostid is not set. Set a unique hostid "
			    "with the zgenhostid(8) command.\n"), name);
		} else {
			char *hostname = "<unknown>";
			uint64_t timestamp = 0;
			uint64_t hostid = 0;

			if (nvlist_exists(config, ZPOOL_CONFIG_HOSTNAME))
				hostname = fnvlist_lookup_string(config,
				    ZPOOL_CONFIG_HOSTNAME);

			if (nvlist_exists(config, ZPOOL_CONFIG_TIMESTAMP))
				timestamp = fnvlist_lookup_uint64(config,
				    ZPOOL_CONFIG_TIMESTAMP);

			if (nvlist_exists(config, ZPOOL_CONFIG_HOSTID))
				hostid = fnvlist_lookup_uint64(config,
				    ZPOOL_CONFIG_HOSTID);

			(void) fprintf(stderr, gettext("cannot import '%s': "
			    "pool was previously in use from another system.\n"
			    "Last accessed by %s (hostid=%lx) at %s"
			    "The pool can be imported, use 'zpool import -f' "
			    "to import the pool.\n"), name, hostname,
			    (unsigned long)hostid, ctime((time_t *)&timestamp));
		}

		return (1);
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
 *       -d	Scan in a specific directory, other than /dev/.  More than
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
 *	 -s	Scan using the default search path, the libblkid cache will
 *	        not be consulted.
 *
 * The import command scans for pools to import, and import pools based on pool
 * name and GUID.  The pool can also be renamed as part of the import process.
 */
int
zpool_do_import(int argc, char **argv)
{
	char **searchdirs = NULL;
	char *env, *envdup = NULL;
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
	boolean_t do_scan = B_FALSE;
	uint64_t pool_state, txg = -1ULL;
	char *cachefile = NULL;
	importargs_t idata = { 0 };
	char *endptr;

	/* check options */
	while ((c = getopt(argc, argv, ":aCc:d:DEfFmnNo:R:stT:VX")) != -1) {
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
			if (add_prop_list_default(zpool_prop_to_name(
			    ZPOOL_PROP_CACHEFILE), "none", &props, B_TRUE))
				goto error;
			break;
		case 's':
			do_scan = B_TRUE;
			break;
		case 't':
			flags |= ZFS_IMPORT_TEMP_NAME;
			if (add_prop_list_default(zpool_prop_to_name(
			    ZPOOL_PROP_CACHEFILE), "none", &props, B_TRUE))
				goto error;
			break;

		case 'T':
			errno = 0;
			txg = strtoull(optarg, &endptr, 0);
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
	}

	/*
	 * Check for the effective uid.  We do this explicitly here because
	 * otherwise any attempt to discover pools will silently fail.
	 */
	if (argc == 0 && geteuid() != 0) {
		(void) fprintf(stderr, gettext("cannot "
		    "discover pools: permission denied\n"));
		if (searchdirs != NULL)
			free(searchdirs);

		nvlist_free(props);
		nvlist_free(policy);
		return (1);
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
		if (errno != 0 || *endptr != '\0') {
			searchname = argv[0];
			searchguid = 0;
		}
		found_config = NULL;

		/*
		 * User specified a name or guid.  Ensure it's unique.
		 */
		idata.unique = B_TRUE;
	}

	/*
	 * Check the environment for the preferred search path.
	 */
	if ((searchdirs == NULL) && (env = getenv("ZPOOL_IMPORT_PATH"))) {
		char *dir;

		envdup = strdup(env);

		dir = strtok(envdup, ":");
		while (dir != NULL) {
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
			searchdirs[nsearch++] = dir;
			dir = strtok(NULL, ":");
		}
	}

	idata.path = searchdirs;
	idata.paths = nsearch;
	idata.poolname = searchname;
	idata.guid = searchguid;
	idata.cachefile = cachefile;
	idata.scan = do_scan;

	/*
	 * Under Linux the zpool_find_import_impl() function leverages the
	 * taskq implementation to parallelize device scanning.  It is
	 * therefore necessary to initialize this functionality for the
	 * duration of the zpool_search_import() function.
	 */
	thread_init();
	pools = zpool_search_import(g_zfs, &idata);
	thread_fini();

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
		if (searchdirs != NULL)
			free(searchdirs);
		if (envdup != NULL)
			free(envdup);
		nvlist_free(policy);
		nvlist_free(pools);
		nvlist_free(props);
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
	if (searchdirs != NULL)
		free(searchdirs);
	if (envdup != NULL)
		free(envdup);

	return (err ? 1 : 0);
}

/*
 * zpool sync [-f] [pool] ...
 *
 * -f (undocumented) force uberblock (and config including zpool cache file)
 *    update.
 *
 * Sync the specified pool(s).
 * Without arguments "zpool sync" will sync all pools.
 * This command initiates TXG sync(s) and will return after the TXG(s) commit.
 *
 */
static int
zpool_do_sync(int argc, char **argv)
{
	int ret;
	boolean_t force = B_FALSE;

	/* check options */
	while ((ret  = getopt(argc, argv, "f")) != -1) {
		switch (ret) {
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

	/* if argc == 0 we will execute zpool_sync_one on all pools */
	ret = for_each_pool(argc, argv, B_FALSE, NULL, zpool_sync_one, &force);

	return (ret);
}

typedef struct iostat_cbdata {
	uint64_t cb_flags;
	int cb_name_flags;
	int cb_namewidth;
	int cb_iteration;
	char **cb_vdev_names; /* Only show these vdevs */
	unsigned int cb_vdev_names_count;
	boolean_t cb_verbose;
	boolean_t cb_literal;
	boolean_t cb_scripted;
	zpool_list_t *cb_list;
	vdev_cmd_data_list_t *vcdl;
} iostat_cbdata_t;

/*  iostat labels */
typedef struct name_and_columns {
	const char *name;	/* Column name */
	unsigned int columns;	/* Center name to this number of columns */
} name_and_columns_t;

#define	IOSTAT_MAX_LABELS	11	/* Max number of labels on one line */

static const name_and_columns_t iostat_top_labels[][IOSTAT_MAX_LABELS] =
{
	[IOS_DEFAULT] = {{"capacity", 2}, {"operations", 2}, {"bandwidth", 2},
	    {NULL}},
	[IOS_LATENCY] = {{"total_wait", 2}, {"disk_wait", 2}, {"syncq_wait", 2},
	    {"asyncq_wait", 2}, {"scrub"}},
	[IOS_QUEUES] = {{"syncq_read", 2}, {"syncq_write", 2},
	    {"asyncq_read", 2}, {"asyncq_write", 2}, {"scrubq_read", 2},
	    {NULL}},
	[IOS_L_HISTO] = {{"total_wait", 2}, {"disk_wait", 2},
	    {"sync_queue", 2}, {"async_queue", 2}, {NULL}},
	[IOS_RQ_HISTO] = {{"sync_read", 2}, {"sync_write", 2},
	    {"async_read", 2}, {"async_write", 2}, {"scrub", 2}, {NULL}},

};

/* Shorthand - if "columns" field not set, default to 1 column */
static const name_and_columns_t iostat_bottom_labels[][IOSTAT_MAX_LABELS] =
{
	[IOS_DEFAULT] = {{"alloc"}, {"free"}, {"read"}, {"write"}, {"read"},
	    {"write"}, {NULL}},
	[IOS_LATENCY] = {{"read"}, {"write"}, {"read"}, {"write"}, {"read"},
	    {"write"}, {"read"}, {"write"}, {"wait"}, {NULL}},
	[IOS_QUEUES] = {{"pend"}, {"activ"}, {"pend"}, {"activ"}, {"pend"},
	    {"activ"}, {"pend"}, {"activ"}, {"pend"}, {"activ"}, {NULL}},
	[IOS_L_HISTO] = {{"read"}, {"write"}, {"read"}, {"write"}, {"read"},
	    {"write"}, {"read"}, {"write"}, {"scrub"}, {NULL}},
	[IOS_RQ_HISTO] = {{"ind"}, {"agg"}, {"ind"}, {"agg"}, {"ind"}, {"agg"},
	    {"ind"}, {"agg"}, {"ind"}, {"agg"}, {NULL}},
};

static const char *histo_to_title[] = {
	[IOS_L_HISTO] = "latency",
	[IOS_RQ_HISTO] = "req_size",
};

/*
 * Return the number of labels in a null-terminated name_and_columns_t
 * array.
 *
 */
static unsigned int
label_array_len(const name_and_columns_t *labels)
{
	int i = 0;

	while (labels[i].name)
		i++;

	return (i);
}

/*
 * Return the number of strings in a null-terminated string array.
 * For example:
 *
 *     const char foo[] = {"bar", "baz", NULL}
 *
 * returns 2
 */
static uint64_t
str_array_len(const char *array[])
{
	uint64_t i = 0;
	while (array[i])
		i++;

	return (i);
}


/*
 * Return a default column width for default/latency/queue columns. This does
 * not include histograms, which have their columns autosized.
 */
static unsigned int
default_column_width(iostat_cbdata_t *cb, enum iostat_type type)
{
	unsigned long column_width = 5; /* Normal niceprint */
	static unsigned long widths[] = {
		/*
		 * Choose some sane default column sizes for printing the
		 * raw numbers.
		 */
		[IOS_DEFAULT] = 15, /* 1PB capacity */
		[IOS_LATENCY] = 10, /* 1B ns = 10sec */
		[IOS_QUEUES] = 6,   /* 1M queue entries */
	};

	if (cb->cb_literal)
		column_width = widths[type];

	return (column_width);
}

/*
 * Print the column labels, i.e:
 *
 *   capacity     operations     bandwidth
 * alloc   free   read  write   read  write  ...
 *
 * If force_column_width is set, use it for the column width.  If not set, use
 * the default column width.
 */
void
print_iostat_labels(iostat_cbdata_t *cb, unsigned int force_column_width,
    const name_and_columns_t labels[][IOSTAT_MAX_LABELS])
{
	int i, idx, s;
	unsigned int text_start, rw_column_width, spaces_to_end;
	uint64_t flags = cb->cb_flags;
	uint64_t f;
	unsigned int column_width = force_column_width;

	/* For each bit set in flags */
	for (f = flags; f; f &= ~(1ULL << idx)) {
		idx = lowbit64(f) - 1;
		if (!force_column_width)
			column_width = default_column_width(cb, idx);
		/* Print our top labels centered over "read  write" label. */
		for (i = 0; i < label_array_len(labels[idx]); i++) {
			const char *name = labels[idx][i].name;
			/*
			 * We treat labels[][].columns == 0 as shorthand
			 * for one column.  It makes writing out the label
			 * tables more concise.
			 */
			unsigned int columns = MAX(1, labels[idx][i].columns);
			unsigned int slen = strlen(name);

			rw_column_width = (column_width * columns) +
			    (2 * (columns - 1));

			text_start = (int)((rw_column_width)/columns -
			    slen/columns);

			printf("  ");	/* Two spaces between columns */

			/* Space from beginning of column to label */
			for (s = 0; s < text_start; s++)
				printf(" ");

			printf("%s", name);

			/* Print space after label to end of column */
			spaces_to_end = rw_column_width - text_start - slen;
			for (s = 0; s < spaces_to_end; s++)
				printf(" ");

		}
	}
}


/*
 * print_cmd_columns - Print custom column titles from -c
 *
 * If the user specified the "zpool status|iostat -c" then print their custom
 * column titles in the header.  For example, print_cmd_columns() would print
 * the "  col1  col2" part of this:
 *
 * $ zpool iostat -vc 'echo col1=val1; echo col2=val2'
 * ...
 *	      capacity     operations     bandwidth
 * pool        alloc   free   read  write   read  write  col1  col2
 * ----------  -----  -----  -----  -----  -----  -----  ----  ----
 * mypool       269K  1008M      0      0    107    946
 *   mirror     269K  1008M      0      0    107    946
 *     sdb         -      -      0      0    102    473  val1  val2
 *     sdc         -      -      0      0      5    473  val1  val2
 * ----------  -----  -----  -----  -----  -----  -----  ----  ----
 */
void
print_cmd_columns(vdev_cmd_data_list_t *vcdl, int use_dashes)
{
	int i, j;
	vdev_cmd_data_t *data = &vcdl->data[0];

	if (vcdl->count == 0 || data == NULL)
		return;

	/*
	 * Each vdev cmd should have the same column names unless the user did
	 * something weird with their cmd.  Just take the column names from the
	 * first vdev and assume it works for all of them.
	 */
	for (i = 0; i < vcdl->uniq_cols_cnt; i++) {
		printf("  ");
		if (use_dashes) {
			for (j = 0; j < vcdl->uniq_cols_width[i]; j++)
				printf("-");
		} else {
			printf("%*s", vcdl->uniq_cols_width[i],
			    vcdl->uniq_cols[i]);
		}
	}
}


/*
 * Utility function to print out a line of dashes like:
 *
 * 	--------------------------------  -----  -----  -----  -----  -----
 *
 * ...or a dashed named-row line like:
 *
 * 	logs                                  -      -      -      -      -
 *
 * @cb:				iostat data
 *
 * @force_column_width		If non-zero, use the value as the column width.
 * 				Otherwise use the default column widths.
 *
 * @name:			Print a dashed named-row line starting
 * 				with @name.  Otherwise, print a regular
 * 				dashed line.
 */
static void
print_iostat_dashes(iostat_cbdata_t *cb, unsigned int force_column_width,
    const char *name)
{
	int i;
	unsigned int namewidth;
	uint64_t flags = cb->cb_flags;
	uint64_t f;
	int idx;
	const name_and_columns_t *labels;
	const char *title;


	if (cb->cb_flags & IOS_ANYHISTO_M) {
		title = histo_to_title[IOS_HISTO_IDX(cb->cb_flags)];
	} else if (cb->cb_vdev_names_count) {
		title = "vdev";
	} else  {
		title = "pool";
	}

	namewidth = MAX(MAX(strlen(title), cb->cb_namewidth),
	    name ? strlen(name) : 0);


	if (name) {
		printf("%-*s", namewidth, name);
	} else {
		for (i = 0; i < namewidth; i++)
			(void) printf("-");
	}

	/* For each bit in flags */
	for (f = flags; f; f &= ~(1ULL << idx)) {
		unsigned int column_width;
		idx = lowbit64(f) - 1;
		if (force_column_width)
			column_width = force_column_width;
		else
			column_width = default_column_width(cb, idx);

		labels = iostat_bottom_labels[idx];
		for (i = 0; i < label_array_len(labels); i++) {
			if (name)
				printf("  %*s-", column_width - 1, " ");
			else
				printf("  %.*s", column_width,
				    "--------------------");
		}
	}
}


static void
print_iostat_separator_impl(iostat_cbdata_t *cb,
    unsigned int force_column_width)
{
	print_iostat_dashes(cb, force_column_width, NULL);
}

static void
print_iostat_separator(iostat_cbdata_t *cb)
{
	print_iostat_separator_impl(cb, 0);
}

static void
print_iostat_header_impl(iostat_cbdata_t *cb, unsigned int force_column_width,
    const char *histo_vdev_name)
{
	unsigned int namewidth;
	const char *title;

	if (cb->cb_flags & IOS_ANYHISTO_M) {
		title = histo_to_title[IOS_HISTO_IDX(cb->cb_flags)];
	} else if (cb->cb_vdev_names_count) {
		title = "vdev";
	} else  {
		title = "pool";
	}

	namewidth = MAX(MAX(strlen(title), cb->cb_namewidth),
	    histo_vdev_name ? strlen(histo_vdev_name) : 0);

	if (histo_vdev_name)
		printf("%-*s", namewidth, histo_vdev_name);
	else
		printf("%*s", namewidth, "");


	print_iostat_labels(cb, force_column_width, iostat_top_labels);
	printf("\n");

	printf("%-*s", namewidth, title);

	print_iostat_labels(cb, force_column_width, iostat_bottom_labels);
	if (cb->vcdl != NULL)
		print_cmd_columns(cb->vcdl, 0);

	printf("\n");

	print_iostat_separator_impl(cb, force_column_width);

	if (cb->vcdl != NULL)
		print_cmd_columns(cb->vcdl, 1);

	printf("\n");
}

static void
print_iostat_header(iostat_cbdata_t *cb)
{
	print_iostat_header_impl(cb, 0, NULL);
}


/*
 * Display a single statistic.
 */
static void
print_one_stat(uint64_t value, enum zfs_nicenum_format format,
    unsigned int column_size, boolean_t scripted)
{
	char buf[64];

	zfs_nicenum_format(value, buf, sizeof (buf), format);

	if (scripted)
		printf("\t%s", buf);
	else
		printf("  %*s", column_size, buf);
}

/*
 * Calculate the default vdev stats
 *
 * Subtract oldvs from newvs, apply a scaling factor, and save the resulting
 * stats into calcvs.
 */
static void
calc_default_iostats(vdev_stat_t *oldvs, vdev_stat_t *newvs,
    vdev_stat_t *calcvs)
{
	int i;

	memcpy(calcvs, newvs, sizeof (*calcvs));
	for (i = 0; i < ARRAY_SIZE(calcvs->vs_ops); i++)
		calcvs->vs_ops[i] = (newvs->vs_ops[i] - oldvs->vs_ops[i]);

	for (i = 0; i < ARRAY_SIZE(calcvs->vs_bytes); i++)
		calcvs->vs_bytes[i] = (newvs->vs_bytes[i] - oldvs->vs_bytes[i]);
}

/*
 * Internal representation of the extended iostats data.
 *
 * The extended iostat stats are exported in nvlists as either uint64_t arrays
 * or single uint64_t's.  We make both look like arrays to make them easier
 * to process.  In order to make single uint64_t's look like arrays, we set
 * __data to the stat data, and then set *data = &__data with count = 1.  Then,
 * we can just use *data and count.
 */
struct stat_array {
	uint64_t *data;
	uint_t count;	/* Number of entries in data[] */
	uint64_t __data; /* Only used when data is a single uint64_t */
};

static uint64_t
stat_histo_max(struct stat_array *nva, unsigned int len)
{
	uint64_t max = 0;
	int i;
	for (i = 0; i < len; i++)
		max = MAX(max, array64_max(nva[i].data, nva[i].count));

	return (max);
}

/*
 * Helper function to lookup a uint64_t array or uint64_t value and store its
 * data as a stat_array.  If the nvpair is a single uint64_t value, then we make
 * it look like a one element array to make it easier to process.
 */
static int
nvpair64_to_stat_array(nvlist_t *nvl, const char *name,
    struct stat_array *nva)
{
	nvpair_t *tmp;
	int ret;

	verify(nvlist_lookup_nvpair(nvl, name, &tmp) == 0);
	switch (nvpair_type(tmp)) {
	case DATA_TYPE_UINT64_ARRAY:
		ret = nvpair_value_uint64_array(tmp, &nva->data, &nva->count);
		break;
	case DATA_TYPE_UINT64:
		ret = nvpair_value_uint64(tmp, &nva->__data);
		nva->data = &nva->__data;
		nva->count = 1;
		break;
	default:
		/* Not a uint64_t */
		ret = EINVAL;
		break;
	}

	return (ret);
}

/*
 * Given a list of nvlist names, look up the extended stats in newnv and oldnv,
 * subtract them, and return the results in a newly allocated stat_array.
 * You must free the returned array after you are done with it with
 * free_calc_stats().
 *
 * Additionally, you can set "oldnv" to NULL if you simply want the newnv
 * values.
 */
static struct stat_array *
calc_and_alloc_stats_ex(const char **names, unsigned int len, nvlist_t *oldnv,
    nvlist_t *newnv)
{
	nvlist_t *oldnvx = NULL, *newnvx;
	struct stat_array *oldnva, *newnva, *calcnva;
	int i, j;
	unsigned int alloc_size = (sizeof (struct stat_array)) * len;

	/* Extract our extended stats nvlist from the main list */
	verify(nvlist_lookup_nvlist(newnv, ZPOOL_CONFIG_VDEV_STATS_EX,
	    &newnvx) == 0);
	if (oldnv) {
		verify(nvlist_lookup_nvlist(oldnv, ZPOOL_CONFIG_VDEV_STATS_EX,
		    &oldnvx) == 0);
	}

	newnva = safe_malloc(alloc_size);
	oldnva = safe_malloc(alloc_size);
	calcnva = safe_malloc(alloc_size);

	for (j = 0; j < len; j++) {
		verify(nvpair64_to_stat_array(newnvx, names[j],
		    &newnva[j]) == 0);
		calcnva[j].count = newnva[j].count;
		alloc_size = calcnva[j].count * sizeof (calcnva[j].data[0]);
		calcnva[j].data = safe_malloc(alloc_size);
		memcpy(calcnva[j].data, newnva[j].data, alloc_size);

		if (oldnvx) {
			verify(nvpair64_to_stat_array(oldnvx, names[j],
			    &oldnva[j]) == 0);
			for (i = 0; i < oldnva[j].count; i++)
				calcnva[j].data[i] -= oldnva[j].data[i];
		}
	}
	free(newnva);
	free(oldnva);
	return (calcnva);
}

static void
free_calc_stats(struct stat_array *nva, unsigned int len)
{
	int i;
	for (i = 0; i < len; i++)
		free(nva[i].data);

	free(nva);
}

static void
print_iostat_histo(struct stat_array *nva, unsigned int len,
    iostat_cbdata_t *cb, unsigned int column_width, unsigned int namewidth,
    double scale)
{
	int i, j;
	char buf[6];
	uint64_t val;
	enum zfs_nicenum_format format;
	unsigned int buckets;
	unsigned int start_bucket;

	if (cb->cb_literal)
		format = ZFS_NICENUM_RAW;
	else
		format = ZFS_NICENUM_1024;

	/* All these histos are the same size, so just use nva[0].count */
	buckets = nva[0].count;

	if (cb->cb_flags & IOS_RQ_HISTO_M) {
		/* Start at 512 - req size should never be lower than this */
		start_bucket = 9;
	} else {
		start_bucket = 0;
	}

	for (j = start_bucket; j < buckets; j++) {
		/* Print histogram bucket label */
		if (cb->cb_flags & IOS_L_HISTO_M) {
			/* Ending range of this bucket */
			val = (1UL << (j + 1)) - 1;
			zfs_nicetime(val, buf, sizeof (buf));
		} else {
			/* Request size (starting range of bucket) */
			val = (1UL << j);
			zfs_nicenum(val, buf, sizeof (buf));
		}

		if (cb->cb_scripted)
			printf("%llu", (u_longlong_t)val);
		else
			printf("%-*s", namewidth, buf);

		/* Print the values on the line */
		for (i = 0; i < len; i++) {
			print_one_stat(nva[i].data[j] * scale, format,
			    column_width, cb->cb_scripted);
		}
		printf("\n");
	}
}

static void
print_solid_separator(unsigned int length)
{
	while (length--)
		printf("-");
	printf("\n");
}

static void
print_iostat_histos(iostat_cbdata_t *cb, nvlist_t *oldnv,
    nvlist_t *newnv, double scale, const char *name)
{
	unsigned int column_width;
	unsigned int namewidth;
	unsigned int entire_width;
	enum iostat_type type;
	struct stat_array *nva;
	const char **names;
	unsigned int names_len;

	/* What type of histo are we? */
	type = IOS_HISTO_IDX(cb->cb_flags);

	/* Get NULL-terminated array of nvlist names for our histo */
	names = vsx_type_to_nvlist[type];
	names_len = str_array_len(names); /* num of names */

	nva = calc_and_alloc_stats_ex(names, names_len, oldnv, newnv);

	if (cb->cb_literal) {
		column_width = MAX(5,
		    (unsigned int) log10(stat_histo_max(nva, names_len)) + 1);
	} else {
		column_width = 5;
	}

	namewidth = MAX(cb->cb_namewidth,
	    strlen(histo_to_title[IOS_HISTO_IDX(cb->cb_flags)]));

	/*
	 * Calculate the entire line width of what we're printing.  The
	 * +2 is for the two spaces between columns:
	 */
	/*	 read  write				*/
	/*	-----  -----				*/
	/*	|___|  <---------- column_width		*/
	/*						*/
	/*	|__________|  <--- entire_width		*/
	/*						*/
	entire_width = namewidth + (column_width + 2) *
	    label_array_len(iostat_bottom_labels[type]);

	if (cb->cb_scripted)
		printf("%s\n", name);
	else
		print_iostat_header_impl(cb, column_width, name);

	print_iostat_histo(nva, names_len, cb, column_width,
	    namewidth, scale);

	free_calc_stats(nva, names_len);
	if (!cb->cb_scripted)
		print_solid_separator(entire_width);
}

/*
 * Calculate the average latency of a power-of-two latency histogram
 */
static uint64_t
single_histo_average(uint64_t *histo, unsigned int buckets)
{
	int i;
	uint64_t count = 0, total = 0;

	for (i = 0; i < buckets; i++) {
		/*
		 * Our buckets are power-of-two latency ranges.  Use the
		 * midpoint latency of each bucket to calculate the average.
		 * For example:
		 *
		 * Bucket          Midpoint
		 * 8ns-15ns:       12ns
		 * 16ns-31ns:      24ns
		 * ...
		 */
		if (histo[i] != 0) {
			total += histo[i] * (((1UL << i) + ((1UL << i)/2)));
			count += histo[i];
		}
	}

	/* Prevent divide by zero */
	return (count == 0 ? 0 : total / count);
}

static void
print_iostat_queues(iostat_cbdata_t *cb, nvlist_t *oldnv,
    nvlist_t *newnv)
{
	int i;
	uint64_t val;
	const char *names[] = {
		ZPOOL_CONFIG_VDEV_SYNC_R_PEND_QUEUE,
		ZPOOL_CONFIG_VDEV_SYNC_R_ACTIVE_QUEUE,
		ZPOOL_CONFIG_VDEV_SYNC_W_PEND_QUEUE,
		ZPOOL_CONFIG_VDEV_SYNC_W_ACTIVE_QUEUE,
		ZPOOL_CONFIG_VDEV_ASYNC_R_PEND_QUEUE,
		ZPOOL_CONFIG_VDEV_ASYNC_R_ACTIVE_QUEUE,
		ZPOOL_CONFIG_VDEV_ASYNC_W_PEND_QUEUE,
		ZPOOL_CONFIG_VDEV_ASYNC_W_ACTIVE_QUEUE,
		ZPOOL_CONFIG_VDEV_SCRUB_PEND_QUEUE,
		ZPOOL_CONFIG_VDEV_SCRUB_ACTIVE_QUEUE,
	};

	struct stat_array *nva;

	unsigned int column_width = default_column_width(cb, IOS_QUEUES);
	enum zfs_nicenum_format format;

	nva = calc_and_alloc_stats_ex(names, ARRAY_SIZE(names), NULL, newnv);

	if (cb->cb_literal)
		format = ZFS_NICENUM_RAW;
	else
		format = ZFS_NICENUM_1024;

	for (i = 0; i < ARRAY_SIZE(names); i++) {
		val = nva[i].data[0];
		print_one_stat(val, format, column_width, cb->cb_scripted);
	}

	free_calc_stats(nva, ARRAY_SIZE(names));
}

static void
print_iostat_latency(iostat_cbdata_t *cb, nvlist_t *oldnv,
    nvlist_t *newnv)
{
	int i;
	uint64_t val;
	const char *names[] = {
		ZPOOL_CONFIG_VDEV_TOT_R_LAT_HISTO,
		ZPOOL_CONFIG_VDEV_TOT_W_LAT_HISTO,
		ZPOOL_CONFIG_VDEV_DISK_R_LAT_HISTO,
		ZPOOL_CONFIG_VDEV_DISK_W_LAT_HISTO,
		ZPOOL_CONFIG_VDEV_SYNC_R_LAT_HISTO,
		ZPOOL_CONFIG_VDEV_SYNC_W_LAT_HISTO,
		ZPOOL_CONFIG_VDEV_ASYNC_R_LAT_HISTO,
		ZPOOL_CONFIG_VDEV_ASYNC_W_LAT_HISTO,
		ZPOOL_CONFIG_VDEV_SCRUB_LAT_HISTO,
	};
	struct stat_array *nva;

	unsigned int column_width = default_column_width(cb, IOS_LATENCY);
	enum zfs_nicenum_format format;

	nva = calc_and_alloc_stats_ex(names, ARRAY_SIZE(names), oldnv, newnv);

	if (cb->cb_literal)
		format = ZFS_NICENUM_RAWTIME;
	else
		format = ZFS_NICENUM_TIME;

	/* Print our avg latencies on the line */
	for (i = 0; i < ARRAY_SIZE(names); i++) {
		/* Compute average latency for a latency histo */
		val = single_histo_average(nva[i].data, nva[i].count);
		print_one_stat(val, format, column_width, cb->cb_scripted);
	}
	free_calc_stats(nva, ARRAY_SIZE(names));
}

/*
 * Print default statistics (capacity/operations/bandwidth)
 */
static void
print_iostat_default(vdev_stat_t *vs, iostat_cbdata_t *cb, double scale)
{
	unsigned int column_width = default_column_width(cb, IOS_DEFAULT);
	enum zfs_nicenum_format format;
	char na;	/* char to print for "not applicable" values */

	if (cb->cb_literal) {
		format = ZFS_NICENUM_RAW;
		na = '0';
	} else {
		format = ZFS_NICENUM_1024;
		na = '-';
	}

	/* only toplevel vdevs have capacity stats */
	if (vs->vs_space == 0) {
		if (cb->cb_scripted)
			printf("\t%c\t%c", na, na);
		else
			printf("  %*c  %*c", column_width, na, column_width,
			    na);
	} else {
		print_one_stat(vs->vs_alloc, format, column_width,
		    cb->cb_scripted);
		print_one_stat(vs->vs_space - vs->vs_alloc, format,
		    column_width, cb->cb_scripted);
	}

	print_one_stat((uint64_t)(vs->vs_ops[ZIO_TYPE_READ] * scale),
	    format, column_width, cb->cb_scripted);
	print_one_stat((uint64_t)(vs->vs_ops[ZIO_TYPE_WRITE] * scale),
	    format, column_width, cb->cb_scripted);
	print_one_stat((uint64_t)(vs->vs_bytes[ZIO_TYPE_READ] * scale),
	    format, column_width, cb->cb_scripted);
	print_one_stat((uint64_t)(vs->vs_bytes[ZIO_TYPE_WRITE] * scale),
	    format, column_width, cb->cb_scripted);
}

/*
 * Print out all the statistics for the given vdev.  This can either be the
 * toplevel configuration, or called recursively.  If 'name' is NULL, then this
 * is a verbose output, and we don't want to display the toplevel pool stats.
 *
 * Returns the number of stat lines printed.
 */
unsigned int
print_vdev_stats(zpool_handle_t *zhp, const char *name, nvlist_t *oldnv,
    nvlist_t *newnv, iostat_cbdata_t *cb, int depth)
{
	nvlist_t **oldchild, **newchild;
	uint_t c, children;
	vdev_stat_t *oldvs, *newvs, *calcvs;
	vdev_stat_t zerovs = { 0 };
	char *vname;
	int i;
	int ret = 0;
	uint64_t tdelta;
	double scale;

	calcvs = safe_malloc(sizeof (*calcvs));

	if (oldnv != NULL) {
		verify(nvlist_lookup_uint64_array(oldnv,
		    ZPOOL_CONFIG_VDEV_STATS, (uint64_t **)&oldvs, &c) == 0);
	} else {
		oldvs = &zerovs;
	}

	/* Do we only want to see a specific vdev? */
	for (i = 0; i < cb->cb_vdev_names_count; i++) {
		/* Yes we do.  Is this the vdev? */
		if (strcmp(name, cb->cb_vdev_names[i]) == 0) {
			/*
			 * This is our vdev.  Since it is the only vdev we
			 * will be displaying, make depth = 0 so that it
			 * doesn't get indented.
			 */
			depth = 0;
			break;
		}
	}

	if (cb->cb_vdev_names_count && (i == cb->cb_vdev_names_count)) {
		/* Couldn't match the name */
		goto children;
	}


	verify(nvlist_lookup_uint64_array(newnv, ZPOOL_CONFIG_VDEV_STATS,
	    (uint64_t **)&newvs, &c) == 0);

	/*
	 * Print the vdev name unless it's is a histogram.  Histograms
	 * display the vdev name in the header itself.
	 */
	if (!(cb->cb_flags & IOS_ANYHISTO_M)) {
		if (cb->cb_scripted) {
			printf("%s", name);
		} else {
			if (strlen(name) + depth > cb->cb_namewidth)
				(void) printf("%*s%s", depth, "", name);
			else
				(void) printf("%*s%s%*s", depth, "", name,
				    (int)(cb->cb_namewidth - strlen(name) -
				    depth), "");
		}
	}

	/* Calculate our scaling factor */
	tdelta = newvs->vs_timestamp - oldvs->vs_timestamp;
	if ((oldvs->vs_timestamp == 0) && (cb->cb_flags & IOS_ANYHISTO_M)) {
		/*
		 * If we specify printing histograms with no time interval, then
		 * print the histogram numbers over the entire lifetime of the
		 * vdev.
		 */
		scale = 1;
	} else {
		if (tdelta == 0)
			scale = 1.0;
		else
			scale = (double)NANOSEC / tdelta;
	}

	if (cb->cb_flags & IOS_DEFAULT_M) {
		calc_default_iostats(oldvs, newvs, calcvs);
		print_iostat_default(calcvs, cb, scale);
	}
	if (cb->cb_flags & IOS_LATENCY_M)
		print_iostat_latency(cb, oldnv, newnv);
	if (cb->cb_flags & IOS_QUEUES_M)
		print_iostat_queues(cb, oldnv, newnv);
	if (cb->cb_flags & IOS_ANYHISTO_M) {
		printf("\n");
		print_iostat_histos(cb, oldnv, newnv, scale, name);
	}

	if (cb->vcdl != NULL) {
		char *path;
		if (nvlist_lookup_string(newnv, ZPOOL_CONFIG_PATH,
		    &path) == 0) {
			printf("  ");
			zpool_print_cmd(cb->vcdl, zpool_get_name(zhp), path);
		}
	}

	if (!(cb->cb_flags & IOS_ANYHISTO_M))
		printf("\n");

	ret++;

children:

	free(calcvs);

	if (!cb->cb_verbose)
		return (ret);

	if (nvlist_lookup_nvlist_array(newnv, ZPOOL_CONFIG_CHILDREN,
	    &newchild, &children) != 0)
		return (ret);

	if (oldnv && nvlist_lookup_nvlist_array(oldnv, ZPOOL_CONFIG_CHILDREN,
	    &oldchild, &c) != 0)
		return (ret);

	for (c = 0; c < children; c++) {
		uint64_t ishole = B_FALSE, islog = B_FALSE;

		(void) nvlist_lookup_uint64(newchild[c], ZPOOL_CONFIG_IS_HOLE,
		    &ishole);

		(void) nvlist_lookup_uint64(newchild[c], ZPOOL_CONFIG_IS_LOG,
		    &islog);

		if (ishole || islog)
			continue;

		vname = zpool_vdev_name(g_zfs, zhp, newchild[c],
		    cb->cb_name_flags);
		ret += print_vdev_stats(zhp, vname, oldnv ? oldchild[c] : NULL,
		    newchild[c], cb, depth + 2);
		free(vname);
	}

	/*
	 * Log device section
	 */

	if (num_logs(newnv) > 0) {
		if ((!(cb->cb_flags & IOS_ANYHISTO_M)) && !cb->cb_scripted &&
		    !cb->cb_vdev_names) {
			print_iostat_dashes(cb, 0, "logs");
		}
		printf("\n");

		for (c = 0; c < children; c++) {
			uint64_t islog = B_FALSE;
			(void) nvlist_lookup_uint64(newchild[c],
			    ZPOOL_CONFIG_IS_LOG, &islog);

			if (islog) {
				vname = zpool_vdev_name(g_zfs, zhp, newchild[c],
				    cb->cb_name_flags);
				ret += print_vdev_stats(zhp, vname, oldnv ?
				    oldchild[c] : NULL, newchild[c],
				    cb, depth + 2);
				free(vname);
			}
		}

	}

	/*
	 * Include level 2 ARC devices in iostat output
	 */
	if (nvlist_lookup_nvlist_array(newnv, ZPOOL_CONFIG_L2CACHE,
	    &newchild, &children) != 0)
		return (ret);

	if (oldnv && nvlist_lookup_nvlist_array(oldnv, ZPOOL_CONFIG_L2CACHE,
	    &oldchild, &c) != 0)
		return (ret);

	if (children > 0) {
		if ((!(cb->cb_flags & IOS_ANYHISTO_M)) && !cb->cb_scripted &&
		    !cb->cb_vdev_names) {
			print_iostat_dashes(cb, 0, "cache");
		}
		printf("\n");

		for (c = 0; c < children; c++) {
			vname = zpool_vdev_name(g_zfs, zhp, newchild[c],
			    cb->cb_name_flags);
			ret += print_vdev_stats(zhp, vname, oldnv ? oldchild[c]
			    : NULL, newchild[c], cb, depth + 2);
			free(vname);
		}
	}

	return (ret);
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
	int ret;

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

	ret = print_vdev_stats(zhp, zpool_get_name(zhp), oldnvroot, newnvroot,
	    cb, 0);
	if ((ret != 0) && !(cb->cb_flags & IOS_ANYHISTO_M) &&
	    !cb->cb_scripted && cb->cb_verbose && !cb->cb_vdev_names_count) {
		print_iostat_separator(cb);
		if (cb->vcdl != NULL) {
			print_cmd_columns(cb->vcdl, 1);
		}
		printf("\n");
	}

	return (ret);
}

static int
get_columns(void)
{
	struct winsize ws;
	int columns = 80;
	int error;

	if (isatty(STDOUT_FILENO)) {
		error = ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws);
		if (error == 0)
			columns = ws.ws_col;
	} else {
		columns = 999;
	}

	return (columns);
}

int
get_namewidth(zpool_handle_t *zhp, void *data)
{
	iostat_cbdata_t *cb = data;
	nvlist_t *config, *nvroot;
	int columns;

	if ((config = zpool_get_config(zhp, NULL)) != NULL) {
		verify(nvlist_lookup_nvlist(config, ZPOOL_CONFIG_VDEV_TREE,
		    &nvroot) == 0);
		unsigned int poolname_len = strlen(zpool_get_name(zhp));
		if (!cb->cb_verbose)
			cb->cb_namewidth = MAX(poolname_len, cb->cb_namewidth);
		else
			cb->cb_namewidth = MAX(poolname_len,
			    max_width(zhp, nvroot, 0, cb->cb_namewidth,
			    cb->cb_name_flags));
	}
	/*
	 * The width must be at least 10, but may be as large as the
	 * column width - 42 so that we can still fit in one line.
	 */
	columns = get_columns();

	if (cb->cb_namewidth < 10)
		cb->cb_namewidth = 10;
	if (cb->cb_namewidth > columns - 42)
		cb->cb_namewidth = columns - 42;

	return (0);
}

/*
 * Parse the input string, get the 'interval' and 'count' value if there is one.
 */
static void
get_interval_count(int *argcp, char **argv, float *iv,
    unsigned long *cnt)
{
	float interval = 0;
	unsigned long count = 0;
	int argc = *argcp;

	/*
	 * Determine if the last argument is an integer or a pool name
	 */
	if (argc > 0 && isnumber(argv[argc - 1])) {
		char *end;

		errno = 0;
		interval = strtof(argv[argc - 1], &end);

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
	if (argc > 0 && isnumber(argv[argc - 1])) {
		char *end;

		errno = 0;
		count = interval;
		interval = strtof(argv[argc - 1], &end);

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
 * Return stat flags that are supported by all pools by both the module and
 * zpool iostat.  "*data" should be initialized to all 0xFFs before running.
 * It will get ANDed down until only the flags that are supported on all pools
 * remain.
 */
static int
get_stat_flags_cb(zpool_handle_t *zhp, void *data)
{
	uint64_t *mask = data;
	nvlist_t *config, *nvroot, *nvx;
	uint64_t flags = 0;
	int i, j;

	config = zpool_get_config(zhp, NULL);
	verify(nvlist_lookup_nvlist(config, ZPOOL_CONFIG_VDEV_TREE,
	    &nvroot) == 0);

	/* Default stats are always supported, but for completeness.. */
	if (nvlist_exists(nvroot, ZPOOL_CONFIG_VDEV_STATS))
		flags |= IOS_DEFAULT_M;

	/* Get our extended stats nvlist from the main list */
	if (nvlist_lookup_nvlist(nvroot, ZPOOL_CONFIG_VDEV_STATS_EX,
	    &nvx) != 0) {
		/*
		 * No extended stats; they're probably running an older
		 * module.  No big deal, we support that too.
		 */
		goto end;
	}

	/* For each extended stat, make sure all its nvpairs are supported */
	for (j = 0; j < ARRAY_SIZE(vsx_type_to_nvlist); j++) {
		if (!vsx_type_to_nvlist[j][0])
			continue;

		/* Start off by assuming the flag is supported, then check */
		flags |= (1ULL << j);
		for (i = 0; vsx_type_to_nvlist[j][i]; i++) {
			if (!nvlist_exists(nvx, vsx_type_to_nvlist[j][i])) {
				/* flag isn't supported */
				flags = flags & ~(1ULL  << j);
				break;
			}
		}
	}
end:
	*mask = *mask & flags;
	return (0);
}

/*
 * Return a bitmask of stats that are supported on all pools by both the module
 * and zpool iostat.
 */
static uint64_t
get_stat_flags(zpool_list_t *list)
{
	uint64_t mask = -1;

	/*
	 * get_stat_flags_cb() will lop off bits from "mask" until only the
	 * flags that are supported on all pools remain.
	 */
	pool_list_iter(list, B_FALSE, get_stat_flags_cb, &mask);
	return (mask);
}

/*
 * Return 1 if cb_data->cb_vdev_names[0] is this vdev's name, 0 otherwise.
 */
static int
is_vdev_cb(zpool_handle_t *zhp, nvlist_t *nv, void *cb_data)
{
	iostat_cbdata_t *cb = cb_data;
	char *name = NULL;
	int ret = 0;

	name = zpool_vdev_name(g_zfs, zhp, nv, cb->cb_name_flags);

	if (strcmp(name, cb->cb_vdev_names[0]) == 0)
		ret = 1; /* match */
	free(name);

	return (ret);
}

/*
 * Returns 1 if cb_data->cb_vdev_names[0] is a vdev name, 0 otherwise.
 */
static int
is_vdev(zpool_handle_t *zhp, void *cb_data)
{
	return (for_each_vdev(zhp, is_vdev_cb, cb_data));
}

/*
 * Check if vdevs are in a pool
 *
 * Return 1 if all argv[] strings are vdev names in pool "pool_name". Otherwise
 * return 0.  If pool_name is NULL, then search all pools.
 */
static int
are_vdevs_in_pool(int argc, char **argv, char *pool_name,
    iostat_cbdata_t *cb)
{
	char **tmp_name;
	int ret = 0;
	int i;
	int pool_count = 0;

	if ((argc == 0) || !*argv)
		return (0);

	if (pool_name)
		pool_count = 1;

	/* Temporarily hijack cb_vdev_names for a second... */
	tmp_name = cb->cb_vdev_names;

	/* Go though our list of prospective vdev names */
	for (i = 0; i < argc; i++) {
		cb->cb_vdev_names = argv + i;

		/* Is this name a vdev in our pools? */
		ret = for_each_pool(pool_count, &pool_name, B_TRUE, NULL,
		    is_vdev, cb);
		if (!ret) {
			/* No match */
			break;
		}
	}

	cb->cb_vdev_names = tmp_name;

	return (ret);
}

static int
is_pool_cb(zpool_handle_t *zhp, void *data)
{
	char *name = data;
	if (strcmp(name, zpool_get_name(zhp)) == 0)
		return (1);

	return (0);
}

/*
 * Do we have a pool named *name?  If so, return 1, otherwise 0.
 */
static int
is_pool(char *name)
{
	return (for_each_pool(0, NULL, B_TRUE, NULL,  is_pool_cb, name));
}

/* Are all our argv[] strings pool names?  If so return 1, 0 otherwise. */
static int
are_all_pools(int argc, char **argv)
{
	if ((argc == 0) || !*argv)
		return (0);

	while (--argc >= 0)
		if (!is_pool(argv[argc]))
			return (0);

	return (1);
}

/*
 * Helper function to print out vdev/pool names we can't resolve.  Used for an
 * error message.
 */
static void
error_list_unresolved_vdevs(int argc, char **argv, char *pool_name,
    iostat_cbdata_t *cb)
{
	int i;
	char *name;
	char *str;
	for (i = 0; i < argc; i++) {
		name = argv[i];

		if (is_pool(name))
			str = gettext("pool");
		else if (are_vdevs_in_pool(1, &name, pool_name, cb))
			str = gettext("vdev in this pool");
		else if (are_vdevs_in_pool(1, &name, NULL, cb))
			str = gettext("vdev in another pool");
		else
			str = gettext("unknown");

		fprintf(stderr, "\t%s (%s)\n", name, str);
	}
}

/*
 * Same as get_interval_count(), but with additional checks to not misinterpret
 * guids as interval/count values.  Assumes VDEV_NAME_GUID is set in
 * cb.cb_name_flags.
 */
static void
get_interval_count_filter_guids(int *argc, char **argv, float *interval,
    unsigned long *count, iostat_cbdata_t *cb)
{
	char **tmpargv = argv;
	int argc_for_interval = 0;

	/* Is the last arg an interval value?  Or a guid? */
	if (*argc >= 1 && !are_vdevs_in_pool(1, &argv[*argc - 1], NULL, cb)) {
		/*
		 * The last arg is not a guid, so it's probably an
		 * interval value.
		 */
		argc_for_interval++;

		if (*argc >= 2 &&
		    !are_vdevs_in_pool(1, &argv[*argc - 2], NULL, cb)) {
			/*
			 * The 2nd to last arg is not a guid, so it's probably
			 * an interval value.
			 */
			argc_for_interval++;
		}
	}

	/* Point to our list of possible intervals */
	tmpargv = &argv[*argc - argc_for_interval];

	*argc = *argc - argc_for_interval;
	get_interval_count(&argc_for_interval, tmpargv,
	    interval, count);
}

/*
 * Floating point sleep().  Allows you to pass in a floating point value for
 * seconds.
 */
static void
fsleep(float sec)
{
	struct timespec req;
	req.tv_sec = floor(sec);
	req.tv_nsec = (sec - (float)req.tv_sec) * NANOSEC;
	nanosleep(&req, NULL);
}

/*
 * Run one of the zpool status/iostat -c scripts with the help (-h) option and
 * print the result.
 *
 * name:	Short name of the script ('iostat').
 * path:	Full path to the script ('/usr/local/etc/zfs/zpool.d/iostat');
 */
static void
print_zpool_script_help(char *name, char *path)
{
	char *argv[] = {path, "-h", NULL};
	char **lines = NULL;
	int lines_cnt = 0;
	int rc;

	rc = libzfs_run_process_get_stdout_nopath(path, argv, NULL, &lines,
	    &lines_cnt);
	if (rc != 0 || lines == NULL || lines_cnt <= 0) {
		if (lines != NULL)
			libzfs_free_str_array(lines, lines_cnt);
		return;
	}

	for (int i = 0; i < lines_cnt; i++)
		if (!is_blank_str(lines[i]))
			printf("  %-14s  %s\n", name, lines[i]);

	libzfs_free_str_array(lines, lines_cnt);
}

/*
 * Go though the zpool status/iostat -c scripts in the user's path, run their
 * help option (-h), and print out the results.
 */
static void
print_zpool_dir_scripts(char *dirpath)
{
	DIR *dir;
	struct dirent *ent;
	char fullpath[MAXPATHLEN];
	struct stat dir_stat;

	if ((dir = opendir(dirpath)) != NULL) {
		/* print all the files and directories within directory */
		while ((ent = readdir(dir)) != NULL) {
			sprintf(fullpath, "%s/%s", dirpath, ent->d_name);

			/* Print the scripts */
			if (stat(fullpath, &dir_stat) == 0)
				if (dir_stat.st_mode & S_IXUSR &&
				    S_ISREG(dir_stat.st_mode))
					print_zpool_script_help(ent->d_name,
					    fullpath);
		}
		closedir(dir);
	}
}

/*
 * Print out help text for all zpool status/iostat -c scripts.
 */
static void
print_zpool_script_list(char *subcommand)
{
	char *dir, *sp;

	printf(gettext("Available 'zpool %s -c' commands:\n"), subcommand);

	sp = zpool_get_cmd_search_path();
	if (sp == NULL)
		return;

	dir = strtok(sp, ":");
	while (dir != NULL) {
		print_zpool_dir_scripts(dir);
		dir = strtok(NULL, ":");
	}

	free(sp);
}

/*
 * zpool iostat [[-c [script1,script2,...]] [-lq]|[-rw]] [-ghHLpPvy] [-n name]
 *              [-T d|u] [[ pool ...]|[pool vdev ...]|[vdev ...]]
 *              [interval [count]]
 *
 *	-c CMD  For each vdev, run command CMD
 *	-g	Display guid for individual vdev name.
 *	-L	Follow links when resolving vdev path name.
 *	-P	Display full path for vdev name.
 *	-v	Display statistics for individual vdevs
 *	-h	Display help
 *	-p	Display values in parsable (exact) format.
 *	-H	Scripted mode.  Don't display headers, and separate properties
 *		by a single tab.
 *	-l	Display average latency
 *	-q	Display queue depths
 *	-w	Display latency histograms
 *	-r	Display request size histogram
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
	float interval = 0;
	unsigned long count = 0;
	zpool_list_t *list;
	boolean_t verbose = B_FALSE;
	boolean_t latency = B_FALSE, l_histo = B_FALSE, rq_histo = B_FALSE;
	boolean_t queues = B_FALSE, parsable = B_FALSE, scripted = B_FALSE;
	boolean_t omit_since_boot = B_FALSE;
	boolean_t guid = B_FALSE;
	boolean_t follow_links = B_FALSE;
	boolean_t full_name = B_FALSE;
	iostat_cbdata_t cb = { 0 };
	char *cmd = NULL;

	/* Used for printing error message */
	const char flag_to_arg[] = {[IOS_LATENCY] = 'l', [IOS_QUEUES] = 'q',
	    [IOS_L_HISTO] = 'w', [IOS_RQ_HISTO] = 'r'};

	uint64_t unsupported_flags;

	/* check options */
	while ((c = getopt(argc, argv, "c:gLPT:vyhplqrwH")) != -1) {
		switch (c) {
		case 'c':
			if (cmd != NULL) {
				fprintf(stderr,
				    gettext("Can't set -c flag twice\n"));
				exit(1);
			}

			if (getenv("ZPOOL_SCRIPTS_ENABLED") != NULL &&
			    !libzfs_envvar_is_set("ZPOOL_SCRIPTS_ENABLED")) {
				fprintf(stderr, gettext(
				    "Can't run -c, disabled by "
				    "ZPOOL_SCRIPTS_ENABLED.\n"));
				exit(1);
			}

			if ((getuid() <= 0 || geteuid() <= 0) &&
			    !libzfs_envvar_is_set("ZPOOL_SCRIPTS_AS_ROOT")) {
				fprintf(stderr, gettext(
				    "Can't run -c with root privileges "
				    "unless ZPOOL_SCRIPTS_AS_ROOT is set.\n"));
				exit(1);
			}
			cmd = optarg;
			verbose = B_TRUE;
			break;
		case 'g':
			guid = B_TRUE;
			break;
		case 'L':
			follow_links = B_TRUE;
			break;
		case 'P':
			full_name = B_TRUE;
			break;
		case 'T':
			get_timestamp_arg(*optarg);
			break;
		case 'v':
			verbose = B_TRUE;
			break;
		case 'p':
			parsable = B_TRUE;
			break;
		case 'l':
			latency = B_TRUE;
			break;
		case 'q':
			queues = B_TRUE;
			break;
		case 'H':
			scripted = B_TRUE;
			break;
		case 'w':
			l_histo = B_TRUE;
			break;
		case 'r':
			rq_histo = B_TRUE;
			break;
		case 'y':
			omit_since_boot = B_TRUE;
			break;
		case 'h':
			usage(B_FALSE);
			break;
		case '?':
			if (optopt == 'c') {
				print_zpool_script_list("iostat");
				exit(0);
			} else {
				fprintf(stderr,
				    gettext("invalid option '%c'\n"), optopt);
			}
			usage(B_FALSE);
		}
	}

	argc -= optind;
	argv += optind;

	cb.cb_literal = parsable;
	cb.cb_scripted = scripted;

	if (guid)
		cb.cb_name_flags |= VDEV_NAME_GUID;
	if (follow_links)
		cb.cb_name_flags |= VDEV_NAME_FOLLOW_LINKS;
	if (full_name)
		cb.cb_name_flags |= VDEV_NAME_PATH;
	cb.cb_iteration = 0;
	cb.cb_namewidth = 0;
	cb.cb_verbose = verbose;

	/* Get our interval and count values (if any) */
	if (guid) {
		get_interval_count_filter_guids(&argc, argv, &interval,
		    &count, &cb);
	} else {
		get_interval_count(&argc, argv, &interval, &count);
	}

	if (argc == 0) {
		/* No args, so just print the defaults. */
	} else if (are_all_pools(argc, argv)) {
		/* All the args are pool names */
	} else if (are_vdevs_in_pool(argc, argv, NULL, &cb)) {
		/* All the args are vdevs */
		cb.cb_vdev_names = argv;
		cb.cb_vdev_names_count = argc;
		argc = 0; /* No pools to process */
	} else if (are_all_pools(1, argv)) {
		/* The first arg is a pool name */
		if (are_vdevs_in_pool(argc - 1, argv + 1, argv[0], &cb)) {
			/* ...and the rest are vdev names */
			cb.cb_vdev_names = argv + 1;
			cb.cb_vdev_names_count = argc - 1;
			argc = 1; /* One pool to process */
		} else {
			fprintf(stderr, gettext("Expected either a list of "));
			fprintf(stderr, gettext("pools, or list of vdevs in"));
			fprintf(stderr, " \"%s\", ", argv[0]);
			fprintf(stderr, gettext("but got:\n"));
			error_list_unresolved_vdevs(argc - 1, argv + 1,
			    argv[0], &cb);
			fprintf(stderr, "\n");
			usage(B_FALSE);
			return (1);
		}
	} else {
		/*
		 * The args don't make sense. The first arg isn't a pool name,
		 * nor are all the args vdevs.
		 */
		fprintf(stderr, gettext("Unable to parse pools/vdevs list.\n"));
		fprintf(stderr, "\n");
		return (1);
	}

	if (cb.cb_vdev_names_count != 0) {
		/*
		 * If user specified vdevs, it implies verbose.
		 */
		cb.cb_verbose = B_TRUE;
	}

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

	if ((l_histo || rq_histo) && (cmd != NULL || latency || queues)) {
		pool_list_free(list);
		(void) fprintf(stderr,
		    gettext("[-r|-w] isn't allowed with [-c|-l|-q]\n"));
		usage(B_FALSE);
		return (1);
	}

	if (l_histo && rq_histo) {
		pool_list_free(list);
		(void) fprintf(stderr,
		    gettext("Only one of [-r|-w] can be passed at a time\n"));
		usage(B_FALSE);
		return (1);
	}

	/*
	 * Enter the main iostat loop.
	 */
	cb.cb_list = list;

	if (l_histo) {
		/*
		 * Histograms tables look out of place when you try to display
		 * them with the other stats, so make a rule that you can only
		 * print histograms by themselves.
		 */
		cb.cb_flags = IOS_L_HISTO_M;
	} else if (rq_histo) {
		cb.cb_flags = IOS_RQ_HISTO_M;
	} else {
		cb.cb_flags = IOS_DEFAULT_M;
		if (latency)
			cb.cb_flags |= IOS_LATENCY_M;
		if (queues)
			cb.cb_flags |= IOS_QUEUES_M;
	}

	/*
	 * See if the module supports all the stats we want to display.
	 */
	unsupported_flags = cb.cb_flags & ~get_stat_flags(list);
	if (unsupported_flags) {
		uint64_t f;
		int idx;
		fprintf(stderr,
		    gettext("The loaded zfs module doesn't support:"));

		/* for each bit set in unsupported_flags */
		for (f = unsupported_flags; f; f &= ~(1ULL << idx)) {
			idx = lowbit64(f) - 1;
			fprintf(stderr, " -%c", flag_to_arg[idx]);
		}

		fprintf(stderr, ".  Try running a newer module.\n");
		pool_list_free(list);

		return (1);
	}

	for (;;) {
		if ((npools = pool_list_count(list)) == 0)
			(void) fprintf(stderr, gettext("no pools available\n"));
		else {
			/*
			 * If this is the first iteration and -y was supplied
			 * we skip any printing.
			 */
			boolean_t skip = (omit_since_boot &&
			    cb.cb_iteration == 0);

			/*
			 * Refresh all statistics.  This is done as an
			 * explicit step before calculating the maximum name
			 * width, so that any * configuration changes are
			 * properly accounted for.
			 */
			(void) pool_list_iter(list, B_FALSE, refresh_iostat,
			    &cb);

			/*
			 * Iterate over all pools to determine the maximum width
			 * for the pool / device name column across all pools.
			 */
			cb.cb_namewidth = 0;
			(void) pool_list_iter(list, B_FALSE, get_namewidth,
			    &cb);

			if (timestamp_fmt != NODATE)
				print_timestamp(timestamp_fmt);

			if (cmd != NULL && cb.cb_verbose &&
			    !(cb.cb_flags & IOS_ANYHISTO_M)) {
				cb.vcdl = all_pools_for_each_vdev_run(argc,
				    argv, cmd, g_zfs, cb.cb_vdev_names,
				    cb.cb_vdev_names_count, cb.cb_name_flags);
			} else {
				cb.vcdl = NULL;
			}

			/*
			 * If it's the first time and we're not skipping it,
			 * or either skip or verbose mode, print the header.
			 *
			 * The histogram code explicitly prints its header on
			 * every vdev, so skip this for histograms.
			 */
			if (((++cb.cb_iteration == 1 && !skip) ||
			    (skip != verbose)) &&
			    (!(cb.cb_flags & IOS_ANYHISTO_M)) &&
			    !cb.cb_scripted)
				print_iostat_header(&cb);

			if (skip) {
				(void) fsleep(interval);
				continue;
			}


			pool_list_iter(list, B_FALSE, print_iostat, &cb);

			/*
			 * If there's more than one pool, and we're not in
			 * verbose mode (which prints a separator for us),
			 * then print a separator.
			 *
			 * In addition, if we're printing specific vdevs then
			 * we also want an ending separator.
			 */
			if (((npools > 1 && !verbose &&
			    !(cb.cb_flags & IOS_ANYHISTO_M)) ||
			    (!(cb.cb_flags & IOS_ANYHISTO_M) &&
			    cb.cb_vdev_names_count)) &&
			    !cb.cb_scripted) {
				print_iostat_separator(&cb);
				if (cb.vcdl != NULL)
					print_cmd_columns(cb.vcdl, 1);
				printf("\n");
			}

			if (cb.vcdl != NULL)
				free_vdev_cmd_data_list(cb.vcdl);

		}

		/*
		 * Flush the output so that redirection to a file isn't buffered
		 * indefinitely.
		 */
		(void) fflush(stdout);

		if (interval == 0)
			break;

		if (count != 0 && --count == 0)
			break;

		(void) fsleep(interval);
	}

	pool_list_free(list);

	return (ret);
}

typedef struct list_cbdata {
	boolean_t	cb_verbose;
	int		cb_name_flags;
	int		cb_namewidth;
	boolean_t	cb_scripted;
	zprop_list_t	*cb_proplist;
	boolean_t	cb_literal;
} list_cbdata_t;

/*
 * Given a list of columns to display, output appropriate headers for each one.
 */
static void
print_header(list_cbdata_t *cb)
{
	zprop_list_t *pl = cb->cb_proplist;
	char headerbuf[ZPOOL_MAXPROPLEN];
	const char *header;
	boolean_t first = B_TRUE;
	boolean_t right_justify;
	size_t width = 0;

	for (; pl != NULL; pl = pl->pl_next) {
		width = pl->pl_width;
		if (first && cb->cb_verbose) {
			/*
			 * Reset the width to accommodate the verbose listing
			 * of devices.
			 */
			width = cb->cb_namewidth;
		}

		if (!first)
			(void) printf("  ");
		else
			first = B_FALSE;

		right_justify = B_FALSE;
		if (pl->pl_prop != ZPROP_INVAL) {
			header = zpool_prop_column_name(pl->pl_prop);
			right_justify = zpool_prop_align_right(pl->pl_prop);
		} else {
			int i;

			for (i = 0; pl->pl_user_prop[i] != '\0'; i++)
				headerbuf[i] = toupper(pl->pl_user_prop[i]);
			headerbuf[i] = '\0';
			header = headerbuf;
		}

		if (pl->pl_next == NULL && !right_justify)
			(void) printf("%s", header);
		else if (right_justify)
			(void) printf("%*s", (int)width, header);
		else
			(void) printf("%-*s", (int)width, header);
	}

	(void) printf("\n");
}

/*
 * Given a pool and a list of properties, print out all the properties according
 * to the described layout.
 */
static void
print_pool(zpool_handle_t *zhp, list_cbdata_t *cb)
{
	zprop_list_t *pl = cb->cb_proplist;
	boolean_t first = B_TRUE;
	char property[ZPOOL_MAXPROPLEN];
	char *propstr;
	boolean_t right_justify;
	size_t width;

	for (; pl != NULL; pl = pl->pl_next) {

		width = pl->pl_width;
		if (first && cb->cb_verbose) {
			/*
			 * Reset the width to accommodate the verbose listing
			 * of devices.
			 */
			width = cb->cb_namewidth;
		}

		if (!first) {
			if (cb->cb_scripted)
				(void) printf("\t");
			else
				(void) printf("  ");
		} else {
			first = B_FALSE;
		}

		right_justify = B_FALSE;
		if (pl->pl_prop != ZPROP_INVAL) {
			if (zpool_get_prop(zhp, pl->pl_prop, property,
			    sizeof (property), NULL, cb->cb_literal) != 0)
				propstr = "-";
			else
				propstr = property;

			right_justify = zpool_prop_align_right(pl->pl_prop);
		} else if ((zpool_prop_feature(pl->pl_user_prop) ||
		    zpool_prop_unsupported(pl->pl_user_prop)) &&
		    zpool_prop_get_feature(zhp, pl->pl_user_prop, property,
		    sizeof (property)) == 0) {
			propstr = property;
		} else {
			propstr = "-";
		}


		/*
		 * If this is being called in scripted mode, or if this is the
		 * last column and it is left-justified, don't include a width
		 * format specifier.
		 */
		if (cb->cb_scripted || (pl->pl_next == NULL && !right_justify))
			(void) printf("%s", propstr);
		else if (right_justify)
			(void) printf("%*s", (int)width, propstr);
		else
			(void) printf("%-*s", (int)width, propstr);
	}

	(void) printf("\n");
}

static void
print_one_column(zpool_prop_t prop, uint64_t value, boolean_t scripted,
    boolean_t valid, enum zfs_nicenum_format format)
{
	char propval[64];
	boolean_t fixed;
	size_t width = zprop_width(prop, &fixed, ZFS_TYPE_POOL);

	switch (prop) {
	case ZPOOL_PROP_EXPANDSZ:
		if (value == 0)
			(void) strlcpy(propval, "-", sizeof (propval));
		else
			zfs_nicenum_format(value, propval, sizeof (propval),
			    format);
		break;
	case ZPOOL_PROP_FRAGMENTATION:
		if (value == ZFS_FRAG_INVALID) {
			(void) strlcpy(propval, "-", sizeof (propval));
		} else if (format == ZFS_NICENUM_RAW) {
			(void) snprintf(propval, sizeof (propval), "%llu",
			    (unsigned long long)value);
		} else {
			(void) snprintf(propval, sizeof (propval), "%llu%%",
			    (unsigned long long)value);
		}
		break;
	case ZPOOL_PROP_CAPACITY:
		if (format == ZFS_NICENUM_RAW)
			(void) snprintf(propval, sizeof (propval), "%llu",
			    (unsigned long long)value);
		else
			(void) snprintf(propval, sizeof (propval), "%llu%%",
			    (unsigned long long)value);
		break;
	default:
		zfs_nicenum_format(value, propval, sizeof (propval), format);
	}

	if (!valid)
		(void) strlcpy(propval, "-", sizeof (propval));

	if (scripted)
		(void) printf("\t%s", propval);
	else
		(void) printf("  %*s", (int)width, propval);
}

void
print_list_stats(zpool_handle_t *zhp, const char *name, nvlist_t *nv,
    list_cbdata_t *cb, int depth)
{
	nvlist_t **child;
	vdev_stat_t *vs;
	uint_t c, children;
	char *vname;
	boolean_t scripted = cb->cb_scripted;
	uint64_t islog = B_FALSE;
	boolean_t haslog = B_FALSE;
	char *dashes = "%-*s      -      -      -         -      -      -\n";

	verify(nvlist_lookup_uint64_array(nv, ZPOOL_CONFIG_VDEV_STATS,
	    (uint64_t **)&vs, &c) == 0);

	if (name != NULL) {
		boolean_t toplevel = (vs->vs_space != 0);
		uint64_t cap;
		enum zfs_nicenum_format format;

		if (cb->cb_literal)
			format = ZFS_NICENUM_RAW;
		else
			format = ZFS_NICENUM_1024;

		if (scripted)
			(void) printf("\t%s", name);
		else if (strlen(name) + depth > cb->cb_namewidth)
			(void) printf("%*s%s", depth, "", name);
		else
			(void) printf("%*s%s%*s", depth, "", name,
			    (int)(cb->cb_namewidth - strlen(name) - depth), "");

		/*
		 * Print the properties for the individual vdevs. Some
		 * properties are only applicable to toplevel vdevs. The
		 * 'toplevel' boolean value is passed to the print_one_column()
		 * to indicate that the value is valid.
		 */
		print_one_column(ZPOOL_PROP_SIZE, vs->vs_space, scripted,
		    toplevel, format);
		print_one_column(ZPOOL_PROP_ALLOCATED, vs->vs_alloc, scripted,
		    toplevel, format);
		print_one_column(ZPOOL_PROP_FREE, vs->vs_space - vs->vs_alloc,
		    scripted, toplevel, format);
		print_one_column(ZPOOL_PROP_EXPANDSZ, vs->vs_esize, scripted,
		    B_TRUE, format);
		print_one_column(ZPOOL_PROP_FRAGMENTATION,
		    vs->vs_fragmentation, scripted,
		    (vs->vs_fragmentation != ZFS_FRAG_INVALID && toplevel),
		    format);
		cap = (vs->vs_space == 0) ? 0 :
		    (vs->vs_alloc * 100 / vs->vs_space);
		print_one_column(ZPOOL_PROP_CAPACITY, cap, scripted, toplevel,
		    format);
		(void) printf("\n");
	}

	if (nvlist_lookup_nvlist_array(nv, ZPOOL_CONFIG_CHILDREN,
	    &child, &children) != 0)
		return;

	for (c = 0; c < children; c++) {
		uint64_t ishole = B_FALSE;

		if (nvlist_lookup_uint64(child[c],
		    ZPOOL_CONFIG_IS_HOLE, &ishole) == 0 && ishole)
			continue;

		if (nvlist_lookup_uint64(child[c],
		    ZPOOL_CONFIG_IS_LOG, &islog) == 0 && islog) {
			haslog = B_TRUE;
			continue;
		}

		vname = zpool_vdev_name(g_zfs, zhp, child[c],
		    cb->cb_name_flags);
		print_list_stats(zhp, vname, child[c], cb, depth + 2);
		free(vname);
	}

	if (haslog == B_TRUE) {
		/* LINTED E_SEC_PRINTF_VAR_FMT */
		(void) printf(dashes, cb->cb_namewidth, "log");
		for (c = 0; c < children; c++) {
			if (nvlist_lookup_uint64(child[c], ZPOOL_CONFIG_IS_LOG,
			    &islog) != 0 || !islog)
				continue;
			vname = zpool_vdev_name(g_zfs, zhp, child[c],
			    cb->cb_name_flags);
			print_list_stats(zhp, vname, child[c], cb, depth + 2);
			free(vname);
		}
	}

	if (nvlist_lookup_nvlist_array(nv, ZPOOL_CONFIG_L2CACHE,
	    &child, &children) == 0 && children > 0) {
		/* LINTED E_SEC_PRINTF_VAR_FMT */
		(void) printf(dashes, cb->cb_namewidth, "cache");
		for (c = 0; c < children; c++) {
			vname = zpool_vdev_name(g_zfs, zhp, child[c],
			    cb->cb_name_flags);
			print_list_stats(zhp, vname, child[c], cb, depth + 2);
			free(vname);
		}
	}

	if (nvlist_lookup_nvlist_array(nv, ZPOOL_CONFIG_SPARES, &child,
	    &children) == 0 && children > 0) {
		/* LINTED E_SEC_PRINTF_VAR_FMT */
		(void) printf(dashes, cb->cb_namewidth, "spare");
		for (c = 0; c < children; c++) {
			vname = zpool_vdev_name(g_zfs, zhp, child[c],
			    cb->cb_name_flags);
			print_list_stats(zhp, vname, child[c], cb, depth + 2);
			free(vname);
		}
	}
}


/*
 * Generic callback function to list a pool.
 */
int
list_callback(zpool_handle_t *zhp, void *data)
{
	list_cbdata_t *cbp = data;
	nvlist_t *config;
	nvlist_t *nvroot;

	config = zpool_get_config(zhp, NULL);

	print_pool(zhp, cbp);
	if (!cbp->cb_verbose)
		return (0);

	verify(nvlist_lookup_nvlist(config, ZPOOL_CONFIG_VDEV_TREE,
	    &nvroot) == 0);
	print_list_stats(zhp, NULL, nvroot, cbp, 0);

	return (0);
}

/*
 * zpool list [-gHLpP] [-o prop[,prop]*] [-T d|u] [pool] ... [interval [count]]
 *
 *	-g	Display guid for individual vdev name.
 *	-H	Scripted mode.  Don't display headers, and separate properties
 *		by a single tab.
 *	-L	Follow links when resolving vdev path name.
 *	-o	List of properties to display.  Defaults to
 *		"name,size,allocated,free,expandsize,fragmentation,capacity,"
 *		"dedupratio,health,altroot"
 * 	-p	Display values in parsable (exact) format.
 *	-P	Display full path for vdev name.
 *	-T	Display a timestamp in date(1) or Unix format
 *
 * List all pools in the system, whether or not they're healthy.  Output space
 * statistics for each one, as well as health status summary.
 */
int
zpool_do_list(int argc, char **argv)
{
	int c;
	int ret = 0;
	list_cbdata_t cb = { 0 };
	static char default_props[] =
	    "name,size,allocated,free,expandsize,fragmentation,capacity,"
	    "dedupratio,health,altroot";
	char *props = default_props;
	float interval = 0;
	unsigned long count = 0;
	zpool_list_t *list;
	boolean_t first = B_TRUE;

	/* check options */
	while ((c = getopt(argc, argv, ":gHLo:pPT:v")) != -1) {
		switch (c) {
		case 'g':
			cb.cb_name_flags |= VDEV_NAME_GUID;
			break;
		case 'H':
			cb.cb_scripted = B_TRUE;
			break;
		case 'L':
			cb.cb_name_flags |= VDEV_NAME_FOLLOW_LINKS;
			break;
		case 'o':
			props = optarg;
			break;
		case 'P':
			cb.cb_name_flags |= VDEV_NAME_PATH;
			break;
		case 'p':
			cb.cb_literal = B_TRUE;
			break;
		case 'T':
			get_timestamp_arg(*optarg);
			break;
		case 'v':
			cb.cb_verbose = B_TRUE;
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

	for (;;) {
		if ((list = pool_list_get(argc, argv, &cb.cb_proplist,
		    &ret)) == NULL)
			return (1);

		if (pool_list_count(list) == 0)
			break;

		if (timestamp_fmt != NODATE)
			print_timestamp(timestamp_fmt);

		if (!cb.cb_scripted && (first || cb.cb_verbose)) {
			print_header(&cb);
			first = B_FALSE;
		}
		ret = pool_list_iter(list, B_TRUE, list_callback, &cb);

		if (interval == 0)
			break;

		if (count != 0 && --count == 0)
			break;

		pool_list_free(list);
		(void) fsleep(interval);
	}

	if (argc == 0 && !cb.cb_scripted && pool_list_count(list) == 0) {
		(void) printf(gettext("no pools available\n"));
		ret = 0;
	}

	pool_list_free(list);
	zprop_free_list(cb.cb_proplist);
	return (ret);
}

static int
zpool_do_attach_or_replace(int argc, char **argv, int replacing)
{
	boolean_t force = B_FALSE;
	int c;
	nvlist_t *nvroot;
	char *poolname, *old_disk, *new_disk;
	zpool_handle_t *zhp;
	nvlist_t *props = NULL;
	char *propval;
	int ret;

	/* check options */
	while ((c = getopt(argc, argv, "fo:")) != -1) {
		switch (c) {
		case 'f':
			force = B_TRUE;
			break;
		case 'o':
			if ((propval = strchr(optarg, '=')) == NULL) {
				(void) fprintf(stderr, gettext("missing "
				    "'=' for -o option\n"));
				usage(B_FALSE);
			}
			*propval = '\0';
			propval++;

			if ((strcmp(optarg, ZPOOL_CONFIG_ASHIFT) != 0) ||
			    (add_prop_list(optarg, propval, &props, B_TRUE)))
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

	if ((zhp = zpool_open(g_zfs, poolname)) == NULL) {
		nvlist_free(props);
		return (1);
	}

	if (zpool_get_config(zhp, NULL) == NULL) {
		(void) fprintf(stderr, gettext("pool '%s' is unavailable\n"),
		    poolname);
		zpool_close(zhp);
		nvlist_free(props);
		return (1);
	}

	/* unless manually specified use "ashift" pool property (if set) */
	if (!nvlist_exists(props, ZPOOL_CONFIG_ASHIFT)) {
		int intval;
		zprop_source_t src;
		char strval[ZPOOL_MAXPROPLEN];

		intval = zpool_get_prop_int(zhp, ZPOOL_PROP_ASHIFT, &src);
		if (src != ZPROP_SRC_DEFAULT) {
			(void) sprintf(strval, "%" PRId32, intval);
			verify(add_prop_list(ZPOOL_CONFIG_ASHIFT, strval,
			    &props, B_TRUE) == 0);
		}
	}

	nvroot = make_root_vdev(zhp, props, force, B_FALSE, replacing, B_FALSE,
	    argc, argv);
	if (nvroot == NULL) {
		zpool_close(zhp);
		nvlist_free(props);
		return (1);
	}

	ret = zpool_vdev_attach(zhp, old_disk, new_disk, nvroot, replacing);

	nvlist_free(props);
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
 * zpool attach [-f] [-o property=value] <pool> <device> <new_device>
 *
 *	-f	Force attach, even if <new_device> appears to be in use.
 *	-o	Set property=value.
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
 * zpool split [-gLnP] [-o prop=val] ...
 *		[-o mntopt] ...
 *		[-R altroot] <pool> <newpool> [<device> ...]
 *
 *	-g      Display guid for individual vdev name.
 *	-L	Follow links when resolving vdev path name.
 *	-n	Do not split the pool, but display the resulting layout if
 *		it were to be split.
 *	-o	Set property=value, or set mount options.
 *	-P	Display full path for vdev name.
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
	flags.name_flags = 0;

	/* check options */
	while ((c = getopt(argc, argv, ":gLR:no:P")) != -1) {
		switch (c) {
		case 'g':
			flags.name_flags |= VDEV_NAME_GUID;
			break;
		case 'L':
			flags.name_flags |= VDEV_NAME_FOLLOW_LINKS;
			break;
		case 'R':
			flags.import = B_TRUE;
			if (add_prop_list(
			    zpool_prop_to_name(ZPOOL_PROP_ALTROOT), optarg,
			    &props, B_TRUE) != 0) {
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
					nvlist_free(props);
					usage(B_FALSE);
				}
			} else {
				mntopts = optarg;
			}
			break;
		case 'P':
			flags.name_flags |= VDEV_NAME_PATH;
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

	if ((zhp = zpool_open(g_zfs, srcpool)) == NULL) {
		nvlist_free(props);
		return (1);
	}

	config = split_mirror_vdev(zhp, newpool, props, flags, argc, argv);
	if (config == NULL) {
		ret = 1;
	} else {
		if (flags.dryrun) {
			(void) printf(gettext("would create '%s' with the "
			    "following layout:\n\n"), newpool);
			print_vdev_tree(NULL, newpool, config, 0, B_FALSE,
			    flags.name_flags);
		}
	}

	zpool_close(zhp);

	if (ret != 0 || flags.dryrun || !flags.import) {
		nvlist_free(config);
		nvlist_free(props);
		return (ret);
	}

	/*
	 * The split was successful. Now we need to open the new
	 * pool and import it.
	 */
	if ((zhp = zpool_open_canfail(g_zfs, newpool)) == NULL) {
		nvlist_free(config);
		nvlist_free(props);
		return (1);
	}
	if (zpool_get_state(zhp) != POOL_STATE_UNAVAIL &&
	    zpool_enable_datasets(zhp, mntopts, 0) != 0) {
		ret = 1;
		(void) fprintf(stderr, gettext("Split was successful, but "
		    "the datasets could not all be mounted\n"));
		(void) fprintf(stderr, gettext("Try doing '%s' with a "
		    "different altroot\n"), "zpool import");
	}
	zpool_close(zhp);
	nvlist_free(config);
	nvlist_free(props);

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
 *	-f	Force the device into a faulted state.
 *
 *	-t	Only take the device off-line temporarily.  The offline/faulted
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
	boolean_t fault = B_FALSE;

	/* check options */
	while ((c = getopt(argc, argv, "ft")) != -1) {
		switch (c) {
		case 'f':
			fault = B_TRUE;
			break;
		case 't':
			istmp = B_TRUE;
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
		if (fault) {
			uint64_t guid = zpool_vdev_path_to_guid(zhp, argv[i]);
			vdev_aux_t aux;
			if (istmp == B_FALSE) {
				/* Force the fault to persist across imports */
				aux = VDEV_AUX_EXTERNAL_PERSIST;
			} else {
				aux = VDEV_AUX_EXTERNAL;
			}

			if (guid == 0 || zpool_vdev_fault(zhp, guid, aux) != 0)
				ret = 1;
		} else {
			if (zpool_vdev_offline(zhp, argv[i], istmp) != 0)
				ret = 1;
		}
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

/*
 * zpool reguid <pool>
 */
int
zpool_do_reguid(int argc, char **argv)
{
	int c;
	char *poolname;
	zpool_handle_t *zhp;
	int ret = 0;

	/* check options */
	while ((c = getopt(argc, argv, "")) != -1) {
		switch (c) {
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

	if (argc > 1) {
		(void) fprintf(stderr, gettext("too many arguments\n"));
		usage(B_FALSE);
	}

	poolname = argv[0];
	if ((zhp = zpool_open(g_zfs, poolname)) == NULL)
		return (1);

	ret = zpool_reguid(zhp);

	zpool_close(zhp);
	return (ret);
}


/*
 * zpool reopen <pool>
 *
 * Reopen the pool so that the kernel can update the sizes of all vdevs.
 */
int
zpool_do_reopen(int argc, char **argv)
{
	int c;
	int ret = 0;
	zpool_handle_t *zhp;
	char *pool;

	/* check options */
	while ((c = getopt(argc, argv, "")) != -1) {
		switch (c) {
		case '?':
			(void) fprintf(stderr, gettext("invalid option '%c'\n"),
			    optopt);
			usage(B_FALSE);
		}
	}

	argc--;
	argv++;

	if (argc < 1) {
		(void) fprintf(stderr, gettext("missing pool name\n"));
		usage(B_FALSE);
	}

	if (argc > 1) {
		(void) fprintf(stderr, gettext("too many arguments\n"));
		usage(B_FALSE);
	}

	pool = argv[0];
	if ((zhp = zpool_open_canfail(g_zfs, pool)) == NULL)
		return (1);

	ret = zpool_reopen(zhp);
	zpool_close(zhp);
	return (ret);
}

typedef struct scrub_cbdata {
	int	cb_type;
	int	cb_argc;
	char	**cb_argv;
	pool_scrub_cmd_t cb_scrub_cmd;
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

	err = zpool_scan(zhp, cb->cb_type, cb->cb_scrub_cmd);

	return (err != 0);
}

/*
 * zpool scrub [-s | -p] <pool> ...
 *
 *	-s	Stop.  Stops any in-progress scrub.
 *	-p	Pause. Pause in-progress scrub.
 */
int
zpool_do_scrub(int argc, char **argv)
{
	int c;
	scrub_cbdata_t cb;

	cb.cb_type = POOL_SCAN_SCRUB;
	cb.cb_scrub_cmd = POOL_SCRUB_NORMAL;

	/* check options */
	while ((c = getopt(argc, argv, "sp")) != -1) {
		switch (c) {
		case 's':
			cb.cb_type = POOL_SCAN_NONE;
			break;
		case 'p':
			cb.cb_scrub_cmd = POOL_SCRUB_PAUSE;
			break;
		case '?':
			(void) fprintf(stderr, gettext("invalid option '%c'\n"),
			    optopt);
			usage(B_FALSE);
		}
	}

	if (cb.cb_type == POOL_SCAN_NONE &&
	    cb.cb_scrub_cmd == POOL_SCRUB_PAUSE) {
		(void) fprintf(stderr, gettext("invalid option combination: "
		    "-s and -p are mutually exclusive\n"));
		usage(B_FALSE);
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

/*
 * Print out detailed scrub status.
 */
void
print_scan_status(pool_scan_stat_t *ps)
{
	time_t start, end, pause;
	uint64_t elapsed, mins_left, hours_left;
	uint64_t pass_exam, examined, total;
	uint_t rate;
	double fraction_done;
	char processed_buf[7], examined_buf[7], total_buf[7], rate_buf[7];

	(void) printf(gettext("  scan: "));

	/* If there's never been a scan, there's not much to say. */
	if (ps == NULL || ps->pss_func == POOL_SCAN_NONE ||
	    ps->pss_func >= POOL_SCAN_FUNCS) {
		(void) printf(gettext("none requested\n"));
		return;
	}

	start = ps->pss_start_time;
	end = ps->pss_end_time;
	pause = ps->pss_pass_scrub_pause;
	zfs_nicebytes(ps->pss_processed, processed_buf, sizeof (processed_buf));

	assert(ps->pss_func == POOL_SCAN_SCRUB ||
	    ps->pss_func == POOL_SCAN_RESILVER);
	/*
	 * Scan is finished or canceled.
	 */
	if (ps->pss_state == DSS_FINISHED) {
		uint64_t minutes_taken = (end - start) / 60;
		char *fmt = NULL;

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
		if (pause == 0) {
			(void) printf(gettext("scrub in progress since %s"),
			    ctime(&start));
		} else {
			char buf[32];
			struct tm *p = localtime(&pause);
			(void) strftime(buf, sizeof (buf), "%a %b %e %T %Y", p);
			(void) printf(gettext("scrub paused since %s\n"), buf);
			(void) printf(gettext("\tscrub started on   %s"),
			    ctime(&start));
		}
	} else if (ps->pss_func == POOL_SCAN_RESILVER) {
		(void) printf(gettext("resilver in progress since %s"),
		    ctime(&start));
	}

	examined = ps->pss_examined ? ps->pss_examined : 1;
	total = ps->pss_to_examine;
	fraction_done = (double)examined / total;

	/* elapsed time for this pass */
	elapsed = time(NULL) - ps->pss_pass_start;
	elapsed -= ps->pss_pass_scrub_spent_paused;
	elapsed = elapsed ? elapsed : 1;
	pass_exam = ps->pss_pass_exam ? ps->pss_pass_exam : 1;
	rate = pass_exam / elapsed;
	rate = rate ? rate : 1;
	mins_left = ((total - examined) / rate) / 60;
	hours_left = mins_left / 60;

	zfs_nicebytes(examined, examined_buf, sizeof (examined_buf));
	zfs_nicebytes(total, total_buf, sizeof (total_buf));

	/*
	 * do not print estimated time if hours_left is more than 30 days
	 * or we have a paused scrub
	 */
	if (pause == 0) {
		zfs_nicebytes(rate, rate_buf, sizeof (rate_buf));
		(void) printf(gettext("\t%s scanned out of %s at %s/s"),
		    examined_buf, total_buf, rate_buf);
		if (hours_left < (30 * 24)) {
			(void) printf(gettext(", %lluh%um to go\n"),
			    (u_longlong_t)hours_left, (uint_t)(mins_left % 60));
		} else {
			(void) printf(gettext(
			    ", (scan is slow, no estimated time)\n"));
		}
	} else {
		(void) printf(gettext("\t%s scanned out of %s\n"),
		    examined_buf, total_buf);
	}

	if (ps->pss_func == POOL_SCAN_RESILVER) {
		(void) printf(gettext("\t%s resilvered, %.2f%% done\n"),
		    processed_buf, 100 * fraction_done);
	} else if (ps->pss_func == POOL_SCAN_SCRUB) {
		(void) printf(gettext("\t%s repaired, %.2f%% done\n"),
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

	if (zpool_get_errlog(zhp, &nverrlist) != 0)
		return;

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
print_spares(zpool_handle_t *zhp, status_cbdata_t *cb, nvlist_t **spares,
    uint_t nspares)
{
	uint_t i;
	char *name;

	if (nspares == 0)
		return;

	(void) printf(gettext("\tspares\n"));

	for (i = 0; i < nspares; i++) {
		name = zpool_vdev_name(g_zfs, zhp, spares[i],
		    cb->cb_name_flags);
		print_status_config(zhp, cb, name, spares[i], 2, B_TRUE);
		free(name);
	}
}

static void
print_l2cache(zpool_handle_t *zhp, status_cbdata_t *cb, nvlist_t **l2cache,
    uint_t nl2cache)
{
	uint_t i;
	char *name;

	if (nl2cache == 0)
		return;

	(void) printf(gettext("\tcache\n"));

	for (i = 0; i < nl2cache; i++) {
		name = zpool_vdev_name(g_zfs, zhp, l2cache[i],
		    cb->cb_name_flags);
		print_status_config(zhp, cb, name, l2cache[i], 2, B_FALSE);
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
	char dspace[6], mspace[6];

	/*
	 * If the pool was faulted then we may not have been able to
	 * obtain the config. Otherwise, if we have anything in the dedup
	 * table continue processing the stats.
	 */
	if (nvlist_lookup_uint64_array(config, ZPOOL_CONFIG_DDT_OBJ_STATS,
	    (uint64_t **)&ddo, &c) != 0)
		return;

	(void) printf("\n");
	(void) printf(gettext(" dedup: "));
	if (ddo->ddo_count == 0) {
		(void) printf(gettext("no DDT entries\n"));
		return;
	}

	zfs_nicebytes(ddo->ddo_dspace, dspace, sizeof (dspace));
	zfs_nicebytes(ddo->ddo_mspace, mspace, sizeof (mspace));
	(void) printf("DDT entries %llu, size %s on disk, %s in core\n",
	    (u_longlong_t)ddo->ddo_count,
	    dspace,
	    mspace);

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
 *         see: http://zfsonlinux.org/msg/ZFS-xxxx-01
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
	zpool_status_t reason;
	zpool_errata_t errata;
	const char *health;
	uint_t c;
	vdev_stat_t *vs;

	config = zpool_get_config(zhp, NULL);
	reason = zpool_get_status(zhp, &msgid, &errata);

	cbp->cb_count++;

	/*
	 * If we were given 'zpool status -x', only report those pools with
	 * problems.
	 */
	if (cbp->cb_explain &&
	    (reason == ZPOOL_STATUS_OK ||
	    reason == ZPOOL_STATUS_VERSION_OLDER ||
	    reason == ZPOOL_STATUS_FEAT_DISABLED)) {
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

	health = zpool_get_state_str(zhp);

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
		(void) printf(gettext("status: The pool is formatted using a "
		    "legacy on-disk format.  The pool can\n\tstill be used, "
		    "but some features are unavailable.\n"));
		(void) printf(gettext("action: Upgrade the pool using 'zpool "
		    "upgrade'.  Once this is done, the\n\tpool will no longer "
		    "be accessible on software that does not support\n\t"
		    "feature flags.\n"));
		break;

	case ZPOOL_STATUS_VERSION_NEWER:
		(void) printf(gettext("status: The pool has been upgraded to a "
		    "newer, incompatible on-disk version.\n\tThe pool cannot "
		    "be accessed on this system.\n"));
		(void) printf(gettext("action: Access the pool from a system "
		    "running more recent software, or\n\trestore the pool from "
		    "backup.\n"));
		break;

	case ZPOOL_STATUS_FEAT_DISABLED:
		(void) printf(gettext("status: Some supported features are not "
		    "enabled on the pool. The pool can\n\tstill be used, but "
		    "some features are unavailable.\n"));
		(void) printf(gettext("action: Enable all features using "
		    "'zpool upgrade'. Once this is done,\n\tthe pool may no "
		    "longer be accessible by software that does not support\n\t"
		    "the features. See zpool-features(5) for details.\n"));
		break;

	case ZPOOL_STATUS_UNSUP_FEAT_READ:
		(void) printf(gettext("status: The pool cannot be accessed on "
		    "this system because it uses the\n\tfollowing feature(s) "
		    "not supported on this system:\n"));
		zpool_print_unsup_feat(config);
		(void) printf("\n");
		(void) printf(gettext("action: Access the pool from a system "
		    "that supports the required feature(s),\n\tor restore the "
		    "pool from backup.\n"));
		break;

	case ZPOOL_STATUS_UNSUP_FEAT_WRITE:
		(void) printf(gettext("status: The pool can only be accessed "
		    "in read-only mode on this system. It\n\tcannot be "
		    "accessed in read-write mode because it uses the "
		    "following\n\tfeature(s) not supported on this system:\n"));
		zpool_print_unsup_feat(config);
		(void) printf("\n");
		(void) printf(gettext("action: The pool cannot be accessed in "
		    "read-write mode. Import the pool with\n"
		    "\t\"-o readonly=on\", access the pool from a system that "
		    "supports the\n\trequired feature(s), or restore the "
		    "pool from backup.\n"));
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

	case ZPOOL_STATUS_IO_FAILURE_MMP:
		(void) printf(gettext("status: The pool is suspended because "
		    "multihost writes failed or were delayed;\n\tanother "
		    "system could import the pool undetected.\n"));
		(void) printf(gettext("action: Make sure the pool's devices "
		    "are connected, then reboot your system and\n\timport the "
		    "pool.\n"));
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
		    "\tWaiting for administrator intervention to fix the "
		    "faulted pool.\n"));
		(void) printf(gettext("action: Either restore the affected "
		    "device(s) and run 'zpool online',\n"
		    "\tor ignore the intent log records by running "
		    "'zpool clear'.\n"));
		break;

	case ZPOOL_STATUS_HOSTID_MISMATCH:
		(void) printf(gettext("status: Mismatch between pool hostid "
		    "and system hostid on imported pool.\n\tThis pool was "
		    "previously imported into a system with a different "
		    "hostid,\n\tand then was verbatim imported into this "
		    "system.\n"));
		(void) printf(gettext("action: Export this pool on all systems "
		    "on which it is imported.\n"
		    "\tThen import it to correct the mismatch.\n"));
		break;

	case ZPOOL_STATUS_ERRATA:
		(void) printf(gettext("status: Errata #%d detected.\n"),
		    errata);

		switch (errata) {
		case ZPOOL_ERRATA_NONE:
			break;

		case ZPOOL_ERRATA_ZOL_2094_SCRUB:
			(void) printf(gettext("action: To correct the issue "
			    "run 'zpool scrub'.\n"));
			break;

		default:
			/*
			 * All errata which allow the pool to be imported
			 * must contain an action message.
			 */
			assert(0);
		}
		break;

	default:
		/*
		 * The remaining errors can't actually be generated, yet.
		 */
		assert(reason == ZPOOL_STATUS_OK);
	}

	if (msgid != NULL)
		(void) printf(gettext("   see: http://zfsonlinux.org/msg/%s\n"),
		    msgid);

	if (config != NULL) {
		uint64_t nerr;
		nvlist_t **spares, **l2cache;
		uint_t nspares, nl2cache;
		pool_scan_stat_t *ps = NULL;

		(void) nvlist_lookup_uint64_array(nvroot,
		    ZPOOL_CONFIG_SCAN_STATS, (uint64_t **)&ps, &c);
		print_scan_status(ps);

		cbp->cb_namewidth = max_width(zhp, nvroot, 0, 0,
		    cbp->cb_name_flags | VDEV_NAME_TYPE_ID);
		if (cbp->cb_namewidth < 10)
			cbp->cb_namewidth = 10;

		(void) printf(gettext("config:\n\n"));
		(void) printf(gettext("\t%-*s  %-8s %5s %5s %5s"),
		    cbp->cb_namewidth, "NAME", "STATE", "READ", "WRITE",
		    "CKSUM");

		if (cbp->vcdl != NULL)
			print_cmd_columns(cbp->vcdl, 0);

		printf("\n");
		print_status_config(zhp, cbp, zpool_get_name(zhp), nvroot, 0,
		    B_FALSE);

		if (num_logs(nvroot) > 0)
			print_logs(zhp, cbp, nvroot);
		if (nvlist_lookup_nvlist_array(nvroot, ZPOOL_CONFIG_L2CACHE,
		    &l2cache, &nl2cache) == 0)
			print_l2cache(zhp, cbp, l2cache, nl2cache);

		if (nvlist_lookup_nvlist_array(nvroot, ZPOOL_CONFIG_SPARES,
		    &spares, &nspares) == 0)
			print_spares(zhp, cbp, spares, nspares);

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
 * zpool status [-c [script1,script2,...]] [-gLPvx] [-T d|u] [pool] ...
 *              [interval [count]]
 *
 *	-c CMD	For each vdev, run command CMD
 *	-g	Display guid for individual vdev name.
 *	-L	Follow links when resolving vdev path name.
 *	-P	Display full path for vdev name.
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
	float interval = 0;
	unsigned long count = 0;
	status_cbdata_t cb = { 0 };
	char *cmd = NULL;

	/* check options */
	while ((c = getopt(argc, argv, "c:gLPvxDT:")) != -1) {
		switch (c) {
		case 'c':
			if (cmd != NULL) {
				fprintf(stderr,
				    gettext("Can't set -c flag twice\n"));
				exit(1);
			}

			if (getenv("ZPOOL_SCRIPTS_ENABLED") != NULL &&
			    !libzfs_envvar_is_set("ZPOOL_SCRIPTS_ENABLED")) {
				fprintf(stderr, gettext(
				    "Can't run -c, disabled by "
				    "ZPOOL_SCRIPTS_ENABLED.\n"));
				exit(1);
			}

			if ((getuid() <= 0 || geteuid() <= 0) &&
			    !libzfs_envvar_is_set("ZPOOL_SCRIPTS_AS_ROOT")) {
				fprintf(stderr, gettext(
				    "Can't run -c with root privileges "
				    "unless ZPOOL_SCRIPTS_AS_ROOT is set.\n"));
				exit(1);
			}
			cmd = optarg;
			break;
		case 'g':
			cb.cb_name_flags |= VDEV_NAME_GUID;
			break;
		case 'L':
			cb.cb_name_flags |= VDEV_NAME_FOLLOW_LINKS;
			break;
		case 'P':
			cb.cb_name_flags |= VDEV_NAME_PATH;
			break;
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
			if (optopt == 'c') {
				print_zpool_script_list("status");
				exit(0);
			} else {
				fprintf(stderr,
				    gettext("invalid option '%c'\n"), optopt);
			}
			usage(B_FALSE);
		}
	}

	argc -= optind;
	argv += optind;

	get_interval_count(&argc, argv, &interval, &count);

	if (argc == 0)
		cb.cb_allpools = B_TRUE;

	cb.cb_first = B_TRUE;
	cb.cb_print_status = B_TRUE;

	for (;;) {
		if (timestamp_fmt != NODATE)
			print_timestamp(timestamp_fmt);

		if (cmd != NULL)
			cb.vcdl = all_pools_for_each_vdev_run(argc, argv, cmd,
			    NULL, NULL, 0, 0);

		ret = for_each_pool(argc, argv, B_TRUE, NULL,
		    status_callback, &cb);

		if (cb.vcdl != NULL)
			free_vdev_cmd_data_list(cb.vcdl);

		if (argc == 0 && cb.cb_count == 0)
			(void) fprintf(stderr, gettext("no pools available\n"));
		else if (cb.cb_explain && cb.cb_first && cb.cb_allpools)
			(void) printf(gettext("all pools are healthy\n"));

		if (ret != 0)
			return (ret);

		if (interval == 0)
			break;

		if (count != 0 && --count == 0)
			break;

		(void) fsleep(interval);
	}

	return (0);
}

typedef struct upgrade_cbdata {
	int	cb_first;
	int	cb_argc;
	uint64_t cb_version;
	char	**cb_argv;
} upgrade_cbdata_t;

static int
check_unsupp_fs(zfs_handle_t *zhp, void *unsupp_fs)
{
	int zfs_version = (int)zfs_prop_get_int(zhp, ZFS_PROP_VERSION);
	int *count = (int *)unsupp_fs;

	if (zfs_version > ZPL_VERSION) {
		(void) printf(gettext("%s (v%d) is not supported by this "
		    "implementation of ZFS.\n"),
		    zfs_get_name(zhp), zfs_version);
		(*count)++;
	}

	zfs_iter_filesystems(zhp, check_unsupp_fs, unsupp_fs);

	zfs_close(zhp);

	return (0);
}

static int
upgrade_version(zpool_handle_t *zhp, uint64_t version)
{
	int ret;
	nvlist_t *config;
	uint64_t oldversion;
	int unsupp_fs = 0;

	config = zpool_get_config(zhp, NULL);
	verify(nvlist_lookup_uint64(config, ZPOOL_CONFIG_VERSION,
	    &oldversion) == 0);

	assert(SPA_VERSION_IS_SUPPORTED(oldversion));
	assert(oldversion < version);

	ret = zfs_iter_root(zpool_get_handle(zhp), check_unsupp_fs, &unsupp_fs);
	if (ret != 0)
		return (ret);

	if (unsupp_fs) {
		(void) fprintf(stderr, gettext("Upgrade not performed due "
		    "to %d unsupported filesystems (max v%d).\n"),
		    unsupp_fs, (int)ZPL_VERSION);
		return (1);
	}

	ret = zpool_upgrade(zhp, version);
	if (ret != 0)
		return (ret);

	if (version >= SPA_VERSION_FEATURES) {
		(void) printf(gettext("Successfully upgraded "
		    "'%s' from version %llu to feature flags.\n"),
		    zpool_get_name(zhp), (u_longlong_t)oldversion);
	} else {
		(void) printf(gettext("Successfully upgraded "
		    "'%s' from version %llu to version %llu.\n"),
		    zpool_get_name(zhp), (u_longlong_t)oldversion,
		    (u_longlong_t)version);
	}

	return (0);
}

static int
upgrade_enable_all(zpool_handle_t *zhp, int *countp)
{
	int i, ret, count;
	boolean_t firstff = B_TRUE;
	nvlist_t *enabled = zpool_get_features(zhp);

	count = 0;
	for (i = 0; i < SPA_FEATURES; i++) {
		const char *fname = spa_feature_table[i].fi_uname;
		const char *fguid = spa_feature_table[i].fi_guid;
		if (!nvlist_exists(enabled, fguid)) {
			char *propname;
			verify(-1 != asprintf(&propname, "feature@%s", fname));
			ret = zpool_set_prop(zhp, propname,
			    ZFS_FEATURE_ENABLED);
			if (ret != 0) {
				free(propname);
				return (ret);
			}
			count++;

			if (firstff) {
				(void) printf(gettext("Enabled the "
				    "following features on '%s':\n"),
				    zpool_get_name(zhp));
				firstff = B_FALSE;
			}
			(void) printf(gettext("  %s\n"), fname);
			free(propname);
		}
	}

	if (countp != NULL)
		*countp = count;
	return (0);
}

static int
upgrade_cb(zpool_handle_t *zhp, void *arg)
{
	upgrade_cbdata_t *cbp = arg;
	nvlist_t *config;
	uint64_t version;
	boolean_t printnl = B_FALSE;
	int ret;

	config = zpool_get_config(zhp, NULL);
	verify(nvlist_lookup_uint64(config, ZPOOL_CONFIG_VERSION,
	    &version) == 0);

	assert(SPA_VERSION_IS_SUPPORTED(version));

	if (version < cbp->cb_version) {
		cbp->cb_first = B_FALSE;
		ret = upgrade_version(zhp, cbp->cb_version);
		if (ret != 0)
			return (ret);
		printnl = B_TRUE;

		/*
		 * If they did "zpool upgrade -a", then we could
		 * be doing ioctls to different pools.  We need
		 * to log this history once to each pool, and bypass
		 * the normal history logging that happens in main().
		 */
		(void) zpool_log_history(g_zfs, history_str);
		log_history = B_FALSE;
	}

	if (cbp->cb_version >= SPA_VERSION_FEATURES) {
		int count;
		ret = upgrade_enable_all(zhp, &count);
		if (ret != 0)
			return (ret);

		if (count > 0) {
			cbp->cb_first = B_FALSE;
			printnl = B_TRUE;
		}
	}

	if (printnl) {
		(void) printf(gettext("\n"));
	}

	return (0);
}

static int
upgrade_list_older_cb(zpool_handle_t *zhp, void *arg)
{
	upgrade_cbdata_t *cbp = arg;
	nvlist_t *config;
	uint64_t version;

	config = zpool_get_config(zhp, NULL);
	verify(nvlist_lookup_uint64(config, ZPOOL_CONFIG_VERSION,
	    &version) == 0);

	assert(SPA_VERSION_IS_SUPPORTED(version));

	if (version < SPA_VERSION_FEATURES) {
		if (cbp->cb_first) {
			(void) printf(gettext("The following pools are "
			    "formatted with legacy version numbers and can\n"
			    "be upgraded to use feature flags.  After "
			    "being upgraded, these pools\nwill no "
			    "longer be accessible by software that does not "
			    "support feature\nflags.\n\n"));
			(void) printf(gettext("VER  POOL\n"));
			(void) printf(gettext("---  ------------\n"));
			cbp->cb_first = B_FALSE;
		}

		(void) printf("%2llu   %s\n", (u_longlong_t)version,
		    zpool_get_name(zhp));
	}

	return (0);
}

static int
upgrade_list_disabled_cb(zpool_handle_t *zhp, void *arg)
{
	upgrade_cbdata_t *cbp = arg;
	nvlist_t *config;
	uint64_t version;

	config = zpool_get_config(zhp, NULL);
	verify(nvlist_lookup_uint64(config, ZPOOL_CONFIG_VERSION,
	    &version) == 0);

	if (version >= SPA_VERSION_FEATURES) {
		int i;
		boolean_t poolfirst = B_TRUE;
		nvlist_t *enabled = zpool_get_features(zhp);

		for (i = 0; i < SPA_FEATURES; i++) {
			const char *fguid = spa_feature_table[i].fi_guid;
			const char *fname = spa_feature_table[i].fi_uname;
			if (!nvlist_exists(enabled, fguid)) {
				if (cbp->cb_first) {
					(void) printf(gettext("\nSome "
					    "supported features are not "
					    "enabled on the following pools. "
					    "Once a\nfeature is enabled the "
					    "pool may become incompatible with "
					    "software\nthat does not support "
					    "the feature. See "
					    "zpool-features(5) for "
					    "details.\n\n"));
					(void) printf(gettext("POOL  "
					    "FEATURE\n"));
					(void) printf(gettext("------"
					    "---------\n"));
					cbp->cb_first = B_FALSE;
				}

				if (poolfirst) {
					(void) printf(gettext("%s\n"),
					    zpool_get_name(zhp));
					poolfirst = B_FALSE;
				}

				(void) printf(gettext("      %s\n"), fname);
			}
			/*
			 * If they did "zpool upgrade -a", then we could
			 * be doing ioctls to different pools.  We need
			 * to log this history once to each pool, and bypass
			 * the normal history logging that happens in main().
			 */
			(void) zpool_log_history(g_zfs, history_str);
			log_history = B_FALSE;
		}
	}

	return (0);
}

/* ARGSUSED */
static int
upgrade_one(zpool_handle_t *zhp, void *data)
{
	boolean_t printnl = B_FALSE;
	upgrade_cbdata_t *cbp = data;
	uint64_t cur_version;
	int ret;

	if (strcmp("log", zpool_get_name(zhp)) == 0) {
		(void) fprintf(stderr, gettext("'log' is now a reserved word\n"
		    "Pool 'log' must be renamed using export and import"
		    " to upgrade.\n"));
		return (1);
	}

	cur_version = zpool_get_prop_int(zhp, ZPOOL_PROP_VERSION, NULL);
	if (cur_version > cbp->cb_version) {
		(void) printf(gettext("Pool '%s' is already formatted "
		    "using more current version '%llu'.\n\n"),
		    zpool_get_name(zhp), (u_longlong_t)cur_version);
		return (0);
	}

	if (cbp->cb_version != SPA_VERSION && cur_version == cbp->cb_version) {
		(void) printf(gettext("Pool '%s' is already formatted "
		    "using version %llu.\n\n"), zpool_get_name(zhp),
		    (u_longlong_t)cbp->cb_version);
		return (0);
	}

	if (cur_version != cbp->cb_version) {
		printnl = B_TRUE;
		ret = upgrade_version(zhp, cbp->cb_version);
		if (ret != 0)
			return (ret);
	}

	if (cbp->cb_version >= SPA_VERSION_FEATURES) {
		int count = 0;
		ret = upgrade_enable_all(zhp, &count);
		if (ret != 0)
			return (ret);

		if (count != 0) {
			printnl = B_TRUE;
		} else if (cur_version == SPA_VERSION) {
			(void) printf(gettext("Pool '%s' already has all "
			    "supported features enabled.\n"),
			    zpool_get_name(zhp));
		}
	}

	if (printnl) {
		(void) printf(gettext("\n"));
	}

	return (0);
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
	boolean_t upgradeall = B_FALSE;
	char *end;


	/* check options */
	while ((c = getopt(argc, argv, ":avV:")) != -1) {
		switch (c) {
		case 'a':
			upgradeall = B_TRUE;
			break;
		case 'v':
			showversions = B_TRUE;
			break;
		case 'V':
			cb.cb_version = strtoll(optarg, &end, 10);
			if (*end != '\0' ||
			    !SPA_VERSION_IS_SUPPORTED(cb.cb_version)) {
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
	} else if (!upgradeall && argc == 0) {
		(void) fprintf(stderr, gettext("-V option is "
		    "incompatible with other arguments\n"));
		usage(B_FALSE);
	}

	if (showversions) {
		if (upgradeall || argc != 0) {
			(void) fprintf(stderr, gettext("-v option is "
			    "incompatible with other arguments\n"));
			usage(B_FALSE);
		}
	} else if (upgradeall) {
		if (argc != 0) {
			(void) fprintf(stderr, gettext("-a option should not "
			    "be used along with a pool name\n"));
			usage(B_FALSE);
		}
	}

	(void) printf(gettext("This system supports ZFS pool feature "
	    "flags.\n\n"));
	if (showversions) {
		int i;

		(void) printf(gettext("The following features are "
		    "supported:\n\n"));
		(void) printf(gettext("FEAT DESCRIPTION\n"));
		(void) printf("----------------------------------------------"
		    "---------------\n");
		for (i = 0; i < SPA_FEATURES; i++) {
			zfeature_info_t *fi = &spa_feature_table[i];
			const char *ro =
			    (fi->fi_flags & ZFEATURE_FLAG_READONLY_COMPAT) ?
			    " (read-only compatible)" : "";

			(void) printf("%-37s%s\n", fi->fi_uname, ro);
			(void) printf("     %s\n", fi->fi_desc);
		}
		(void) printf("\n");

		(void) printf(gettext("The following legacy versions are also "
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
	} else if (argc == 0 && upgradeall) {
		cb.cb_first = B_TRUE;
		ret = zpool_iter(g_zfs, upgrade_cb, &cb);
		if (ret == 0 && cb.cb_first) {
			if (cb.cb_version == SPA_VERSION) {
				(void) printf(gettext("All pools are already "
				    "formatted using feature flags.\n\n"));
				(void) printf(gettext("Every feature flags "
				    "pool already has all supported features "
				    "enabled.\n"));
			} else {
				(void) printf(gettext("All pools are already "
				    "formatted with version %llu or higher.\n"),
				    (u_longlong_t)cb.cb_version);
			}
		}
	} else if (argc == 0) {
		cb.cb_first = B_TRUE;
		ret = zpool_iter(g_zfs, upgrade_list_older_cb, &cb);
		assert(ret == 0);

		if (cb.cb_first) {
			(void) printf(gettext("All pools are formatted "
			    "using feature flags.\n\n"));
		} else {
			(void) printf(gettext("\nUse 'zpool upgrade -v' "
			    "for a list of available legacy versions.\n"));
		}

		cb.cb_first = B_TRUE;
		ret = zpool_iter(g_zfs, upgrade_list_disabled_cb, &cb);
		assert(ret == 0);

		if (cb.cb_first) {
			(void) printf(gettext("Every feature flags pool has "
			    "all supported features enabled.\n"));
		} else {
			(void) printf(gettext("\n"));
		}
	} else {
		ret = for_each_pool(argc, argv, B_FALSE, NULL,
		    upgrade_one, &cb);
	}

	return (ret);
}

typedef struct hist_cbdata {
	boolean_t first;
	boolean_t longfmt;
	boolean_t internal;
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
	int ret, i;
	hist_cbdata_t *cb = (hist_cbdata_t *)data;

	cb->first = B_FALSE;

	(void) printf(gettext("History for '%s':\n"), zpool_get_name(zhp));

	if ((ret = zpool_get_history(zhp, &nvhis)) != 0)
		return (ret);

	verify(nvlist_lookup_nvlist_array(nvhis, ZPOOL_HIST_RECORD,
	    &records, &numrecords) == 0);
	for (i = 0; i < numrecords; i++) {
		nvlist_t *rec = records[i];
		char tbuf[30] = "";

		if (nvlist_exists(rec, ZPOOL_HIST_TIME)) {
			time_t tsec;
			struct tm t;

			tsec = fnvlist_lookup_uint64(records[i],
			    ZPOOL_HIST_TIME);
			(void) localtime_r(&tsec, &t);
			(void) strftime(tbuf, sizeof (tbuf), "%F.%T", &t);
		}

		if (nvlist_exists(rec, ZPOOL_HIST_CMD)) {
			(void) printf("%s %s", tbuf,
			    fnvlist_lookup_string(rec, ZPOOL_HIST_CMD));
		} else if (nvlist_exists(rec, ZPOOL_HIST_INT_EVENT)) {
			int ievent =
			    fnvlist_lookup_uint64(rec, ZPOOL_HIST_INT_EVENT);
			if (!cb->internal)
				continue;
			if (ievent >= ZFS_NUM_LEGACY_HISTORY_EVENTS) {
				(void) printf("%s unrecognized record:\n",
				    tbuf);
				dump_nvlist(rec, 4);
				continue;
			}
			(void) printf("%s [internal %s txg:%lld] %s", tbuf,
			    zfs_history_event_names[ievent],
			    (longlong_t)fnvlist_lookup_uint64(
			    rec, ZPOOL_HIST_TXG),
			    fnvlist_lookup_string(rec, ZPOOL_HIST_INT_STR));
		} else if (nvlist_exists(rec, ZPOOL_HIST_INT_NAME)) {
			if (!cb->internal)
				continue;
			(void) printf("%s [txg:%lld] %s", tbuf,
			    (longlong_t)fnvlist_lookup_uint64(
			    rec, ZPOOL_HIST_TXG),
			    fnvlist_lookup_string(rec, ZPOOL_HIST_INT_NAME));
			if (nvlist_exists(rec, ZPOOL_HIST_DSNAME)) {
				(void) printf(" %s (%llu)",
				    fnvlist_lookup_string(rec,
				    ZPOOL_HIST_DSNAME),
				    (u_longlong_t)fnvlist_lookup_uint64(rec,
				    ZPOOL_HIST_DSID));
			}
			(void) printf(" %s", fnvlist_lookup_string(rec,
			    ZPOOL_HIST_INT_STR));
		} else if (nvlist_exists(rec, ZPOOL_HIST_IOCTL)) {
			if (!cb->internal)
				continue;
			(void) printf("%s ioctl %s\n", tbuf,
			    fnvlist_lookup_string(rec, ZPOOL_HIST_IOCTL));
			if (nvlist_exists(rec, ZPOOL_HIST_INPUT_NVL)) {
				(void) printf("    input:\n");
				dump_nvlist(fnvlist_lookup_nvlist(rec,
				    ZPOOL_HIST_INPUT_NVL), 8);
			}
			if (nvlist_exists(rec, ZPOOL_HIST_OUTPUT_NVL)) {
				(void) printf("    output:\n");
				dump_nvlist(fnvlist_lookup_nvlist(rec,
				    ZPOOL_HIST_OUTPUT_NVL), 8);
			}
		} else {
			if (!cb->internal)
				continue;
			(void) printf("%s unrecognized record:\n", tbuf);
			dump_nvlist(rec, 4);
		}

		if (!cb->longfmt) {
			(void) printf("\n");
			continue;
		}
		(void) printf(" [");
		if (nvlist_exists(rec, ZPOOL_HIST_WHO)) {
			uid_t who = fnvlist_lookup_uint64(rec, ZPOOL_HIST_WHO);
			struct passwd *pwd = getpwuid(who);
			(void) printf("user %d ", (int)who);
			if (pwd != NULL)
				(void) printf("(%s) ", pwd->pw_name);
		}
		if (nvlist_exists(rec, ZPOOL_HIST_HOST)) {
			(void) printf("on %s",
			    fnvlist_lookup_string(rec, ZPOOL_HIST_HOST));
		}
		if (nvlist_exists(rec, ZPOOL_HIST_ZONE)) {
			(void) printf(":%s",
			    fnvlist_lookup_string(rec, ZPOOL_HIST_ZONE));
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
			cbdata.longfmt = B_TRUE;
			break;
		case 'i':
			cbdata.internal = B_TRUE;
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
		(void) fprintf(stderr, gettext("no pools available\n"));
		return (0);
	}

	return (ret);
}

typedef struct ev_opts {
	int verbose;
	int scripted;
	int follow;
	int clear;
} ev_opts_t;

static void
zpool_do_events_short(nvlist_t *nvl)
{
	char ctime_str[26], str[32], *ptr;
	int64_t *tv;
	uint_t n;

	verify(nvlist_lookup_int64_array(nvl, FM_EREPORT_TIME, &tv, &n) == 0);
	memset(str, ' ', 32);
	(void) ctime_r((const time_t *)&tv[0], ctime_str);
	(void) strncpy(str, ctime_str+4,  6);		/* 'Jun 30' */
	(void) strncpy(str+7, ctime_str+20, 4);		/* '1993' */
	(void) strncpy(str+12, ctime_str+11, 8);	/* '21:49:08' */
	(void) sprintf(str+20, ".%09lld", (longlong_t)tv[1]); /* '.123456789' */
	(void) printf(gettext("%s "), str);

	verify(nvlist_lookup_string(nvl, FM_CLASS, &ptr) == 0);
	(void) printf(gettext("%s\n"), ptr);
}

static void
zpool_do_events_nvprint(nvlist_t *nvl, int depth)
{
	nvpair_t *nvp;

	for (nvp = nvlist_next_nvpair(nvl, NULL);
	    nvp != NULL; nvp = nvlist_next_nvpair(nvl, nvp)) {

		data_type_t type = nvpair_type(nvp);
		const char *name = nvpair_name(nvp);

		boolean_t b;
		uint8_t i8;
		uint16_t i16;
		uint32_t i32;
		uint64_t i64;
		char *str;
		nvlist_t *cnv;

		printf(gettext("%*s%s = "), depth, "", name);

		switch (type) {
		case DATA_TYPE_BOOLEAN:
			printf(gettext("%s"), "1");
			break;

		case DATA_TYPE_BOOLEAN_VALUE:
			(void) nvpair_value_boolean_value(nvp, &b);
			printf(gettext("%s"), b ? "1" : "0");
			break;

		case DATA_TYPE_BYTE:
			(void) nvpair_value_byte(nvp, &i8);
			printf(gettext("0x%x"), i8);
			break;

		case DATA_TYPE_INT8:
			(void) nvpair_value_int8(nvp, (void *)&i8);
			printf(gettext("0x%x"), i8);
			break;

		case DATA_TYPE_UINT8:
			(void) nvpair_value_uint8(nvp, &i8);
			printf(gettext("0x%x"), i8);
			break;

		case DATA_TYPE_INT16:
			(void) nvpair_value_int16(nvp, (void *)&i16);
			printf(gettext("0x%x"), i16);
			break;

		case DATA_TYPE_UINT16:
			(void) nvpair_value_uint16(nvp, &i16);
			printf(gettext("0x%x"), i16);
			break;

		case DATA_TYPE_INT32:
			(void) nvpair_value_int32(nvp, (void *)&i32);
			printf(gettext("0x%x"), i32);
			break;

		case DATA_TYPE_UINT32:
			(void) nvpair_value_uint32(nvp, &i32);
			printf(gettext("0x%x"), i32);
			break;

		case DATA_TYPE_INT64:
			(void) nvpair_value_int64(nvp, (void *)&i64);
			printf(gettext("0x%llx"), (u_longlong_t)i64);
			break;

		case DATA_TYPE_UINT64:
			(void) nvpair_value_uint64(nvp, &i64);
			/*
			 * translate vdev state values to readable
			 * strings to aide zpool events consumers
			 */
			if (strcmp(name,
			    FM_EREPORT_PAYLOAD_ZFS_VDEV_STATE) == 0 ||
			    strcmp(name,
			    FM_EREPORT_PAYLOAD_ZFS_VDEV_LASTSTATE) == 0) {
				printf(gettext("\"%s\" (0x%llx)"),
				    zpool_state_to_name(i64, VDEV_AUX_NONE),
				    (u_longlong_t)i64);
			} else {
				printf(gettext("0x%llx"), (u_longlong_t)i64);
			}
			break;

		case DATA_TYPE_HRTIME:
			(void) nvpair_value_hrtime(nvp, (void *)&i64);
			printf(gettext("0x%llx"), (u_longlong_t)i64);
			break;

		case DATA_TYPE_STRING:
			(void) nvpair_value_string(nvp, &str);
			printf(gettext("\"%s\""), str ? str : "<NULL>");
			break;

		case DATA_TYPE_NVLIST:
			printf(gettext("(embedded nvlist)\n"));
			(void) nvpair_value_nvlist(nvp, &cnv);
			zpool_do_events_nvprint(cnv, depth + 8);
			printf(gettext("%*s(end %s)"), depth, "", name);
			break;

		case DATA_TYPE_NVLIST_ARRAY: {
			nvlist_t **val;
			uint_t i, nelem;

			(void) nvpair_value_nvlist_array(nvp, &val, &nelem);
			printf(gettext("(%d embedded nvlists)\n"), nelem);
			for (i = 0; i < nelem; i++) {
				printf(gettext("%*s%s[%d] = %s\n"),
				    depth, "", name, i, "(embedded nvlist)");
				zpool_do_events_nvprint(val[i], depth + 8);
				printf(gettext("%*s(end %s[%i])\n"),
				    depth, "", name, i);
			}
			printf(gettext("%*s(end %s)\n"), depth, "", name);
			}
			break;

		case DATA_TYPE_INT8_ARRAY: {
			int8_t *val;
			uint_t i, nelem;

			(void) nvpair_value_int8_array(nvp, &val, &nelem);
			for (i = 0; i < nelem; i++)
				printf(gettext("0x%x "), val[i]);

			break;
			}

		case DATA_TYPE_UINT8_ARRAY: {
			uint8_t *val;
			uint_t i, nelem;

			(void) nvpair_value_uint8_array(nvp, &val, &nelem);
			for (i = 0; i < nelem; i++)
				printf(gettext("0x%x "), val[i]);

			break;
			}

		case DATA_TYPE_INT16_ARRAY: {
			int16_t *val;
			uint_t i, nelem;

			(void) nvpair_value_int16_array(nvp, &val, &nelem);
			for (i = 0; i < nelem; i++)
				printf(gettext("0x%x "), val[i]);

			break;
			}

		case DATA_TYPE_UINT16_ARRAY: {
			uint16_t *val;
			uint_t i, nelem;

			(void) nvpair_value_uint16_array(nvp, &val, &nelem);
			for (i = 0; i < nelem; i++)
				printf(gettext("0x%x "), val[i]);

			break;
			}

		case DATA_TYPE_INT32_ARRAY: {
			int32_t *val;
			uint_t i, nelem;

			(void) nvpair_value_int32_array(nvp, &val, &nelem);
			for (i = 0; i < nelem; i++)
				printf(gettext("0x%x "), val[i]);

			break;
			}

		case DATA_TYPE_UINT32_ARRAY: {
			uint32_t *val;
			uint_t i, nelem;

			(void) nvpair_value_uint32_array(nvp, &val, &nelem);
			for (i = 0; i < nelem; i++)
				printf(gettext("0x%x "), val[i]);

			break;
			}

		case DATA_TYPE_INT64_ARRAY: {
			int64_t *val;
			uint_t i, nelem;

			(void) nvpair_value_int64_array(nvp, &val, &nelem);
			for (i = 0; i < nelem; i++)
				printf(gettext("0x%llx "),
				    (u_longlong_t)val[i]);

			break;
			}

		case DATA_TYPE_UINT64_ARRAY: {
			uint64_t *val;
			uint_t i, nelem;

			(void) nvpair_value_uint64_array(nvp, &val, &nelem);
			for (i = 0; i < nelem; i++)
				printf(gettext("0x%llx "),
				    (u_longlong_t)val[i]);

			break;
			}

		case DATA_TYPE_STRING_ARRAY: {
			char **str;
			uint_t i, nelem;

			(void) nvpair_value_string_array(nvp, &str, &nelem);
			for (i = 0; i < nelem; i++)
				printf(gettext("\"%s\" "),
				    str[i] ? str[i] : "<NULL>");

			break;
			}

		case DATA_TYPE_BOOLEAN_ARRAY:
		case DATA_TYPE_BYTE_ARRAY:
		case DATA_TYPE_DOUBLE:
		case DATA_TYPE_UNKNOWN:
			printf(gettext("<unknown>"));
			break;
		}

		printf(gettext("\n"));
	}
}

static int
zpool_do_events_next(ev_opts_t *opts)
{
	nvlist_t *nvl;
	int zevent_fd, ret, dropped;

	zevent_fd = open(ZFS_DEV, O_RDWR);
	VERIFY(zevent_fd >= 0);

	if (!opts->scripted)
		(void) printf(gettext("%-30s %s\n"), "TIME", "CLASS");

	while (1) {
		ret = zpool_events_next(g_zfs, &nvl, &dropped,
		    (opts->follow ? ZEVENT_NONE : ZEVENT_NONBLOCK), zevent_fd);
		if (ret || nvl == NULL)
			break;

		if (dropped > 0)
			(void) printf(gettext("dropped %d events\n"), dropped);

		zpool_do_events_short(nvl);

		if (opts->verbose) {
			zpool_do_events_nvprint(nvl, 8);
			printf(gettext("\n"));
		}
		(void) fflush(stdout);

		nvlist_free(nvl);
	}

	VERIFY(0 == close(zevent_fd));

	return (ret);
}

static int
zpool_do_events_clear(ev_opts_t *opts)
{
	int count, ret;

	ret = zpool_events_clear(g_zfs, &count);
	if (!ret)
		(void) printf(gettext("cleared %d events\n"), count);

	return (ret);
}

/*
 * zpool events [-vfc]
 *
 * Displays events logs by ZFS.
 */
int
zpool_do_events(int argc, char **argv)
{
	ev_opts_t opts = { 0 };
	int ret;
	int c;

	/* check options */
	while ((c = getopt(argc, argv, "vHfc")) != -1) {
		switch (c) {
		case 'v':
			opts.verbose = 1;
			break;
		case 'H':
			opts.scripted = 1;
			break;
		case 'f':
			opts.follow = 1;
			break;
		case 'c':
			opts.clear = 1;
			break;
		case '?':
			(void) fprintf(stderr, gettext("invalid option '%c'\n"),
			    optopt);
			usage(B_FALSE);
		}
	}
	argc -= optind;
	argv += optind;

	if (opts.clear)
		ret = zpool_do_events_clear(&opts);
	else
		ret = zpool_do_events_next(&opts);

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

		if (pl->pl_prop == ZPROP_INVAL &&
		    (zpool_prop_feature(pl->pl_user_prop) ||
		    zpool_prop_unsupported(pl->pl_user_prop))) {
			srctype = ZPROP_SRC_LOCAL;

			if (zpool_prop_get_feature(zhp, pl->pl_user_prop,
			    value, sizeof (value)) == 0) {
				zprop_print_one_property(zpool_get_name(zhp),
				    cbp, pl->pl_user_prop, value, srctype,
				    NULL, NULL);
			}
		} else {
			if (zpool_get_prop(zhp, pl->pl_prop, value,
			    sizeof (value), &srctype, cbp->cb_literal) != 0)
				continue;

			zprop_print_one_property(zpool_get_name(zhp), cbp,
			    zpool_prop_to_name(pl->pl_prop), value, srctype,
			    NULL, NULL);
		}
	}
	return (0);
}

/*
 * zpool get [-Hp] [-o "all" | field[,...]] <"all" | property[,...]> <pool> ...
 *
 *	-H	Scripted mode.  Don't display headers, and separate properties
 *		by a single tab.
 *	-o	List of columns to display.  Defaults to
 *		"name,property,value,source".
 * 	-p	Display values in parsable (exact) format.
 *
 * Get properties of pools in the system. Output space statistics
 * for each one as well as other attributes.
 */
int
zpool_do_get(int argc, char **argv)
{
	zprop_get_cbdata_t cb = { 0 };
	zprop_list_t fake_name = { 0 };
	int ret;
	int c, i;
	char *value;

	cb.cb_first = B_TRUE;

	/*
	 * Set up default columns and sources.
	 */
	cb.cb_sources = ZPROP_SRC_ALL;
	cb.cb_columns[0] = GET_COL_NAME;
	cb.cb_columns[1] = GET_COL_PROPERTY;
	cb.cb_columns[2] = GET_COL_VALUE;
	cb.cb_columns[3] = GET_COL_SOURCE;
	cb.cb_type = ZFS_TYPE_POOL;

	/* check options */
	while ((c = getopt(argc, argv, ":Hpo:")) != -1) {
		switch (c) {
		case 'p':
			cb.cb_literal = B_TRUE;
			break;
		case 'H':
			cb.cb_scripted = B_TRUE;
			break;
		case 'o':
			bzero(&cb.cb_columns, sizeof (cb.cb_columns));
			i = 0;
			while (*optarg != '\0') {
				static char *col_subopts[] =
				{ "name", "property", "value", "source",
				"all", NULL };

				if (i == ZFS_GET_NCOLS) {
					(void) fprintf(stderr, gettext("too "
					"many fields given to -o "
					"option\n"));
					usage(B_FALSE);
				}

				switch (getsubopt(&optarg, col_subopts,
				    &value)) {
				case 0:
					cb.cb_columns[i++] = GET_COL_NAME;
					break;
				case 1:
					cb.cb_columns[i++] = GET_COL_PROPERTY;
					break;
				case 2:
					cb.cb_columns[i++] = GET_COL_VALUE;
					break;
				case 3:
					cb.cb_columns[i++] = GET_COL_SOURCE;
					break;
				case 4:
					if (i > 0) {
						(void) fprintf(stderr,
						    gettext("\"all\" conflicts "
						    "with specific fields "
						    "given to -o option\n"));
						usage(B_FALSE);
					}
					cb.cb_columns[0] = GET_COL_NAME;
					cb.cb_columns[1] = GET_COL_PROPERTY;
					cb.cb_columns[2] = GET_COL_VALUE;
					cb.cb_columns[3] = GET_COL_SOURCE;
					i = ZFS_GET_NCOLS;
					break;
				default:
					(void) fprintf(stderr,
					    gettext("invalid column name "
					    "'%s'\n"), value);
					usage(B_FALSE);
				}
			}
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
		(void) fprintf(stderr, gettext("missing property "
		    "argument\n"));
		usage(B_FALSE);
	}

	if (zprop_get_list(g_zfs, argv[0], &cb.cb_proplist,
	    ZFS_TYPE_POOL) != 0)
		usage(B_FALSE);

	argc--;
	argv++;

	if (cb.cb_proplist != NULL) {
		fake_name.pl_prop = ZPOOL_PROP_NAME;
		fake_name.pl_width = strlen(gettext("NAME"));
		fake_name.pl_next = cb.cb_proplist;
		cb.cb_proplist = &fake_name;
	}

	ret = for_each_pool(argc, argv, B_TRUE, &cb.cb_proplist,
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
	int ret = 0;
	int i = 0;
	char *cmdname;
	char **newargv;

	(void) setlocale(LC_ALL, "");
	(void) textdomain(TEXT_DOMAIN);
	srand(time(NULL));

	dprintf_setup(&argc, argv);

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
	if ((strcmp(cmdname, "-?") == 0) || strcmp(cmdname, "--help") == 0)
		usage(B_TRUE);

	if ((g_zfs = libzfs_init()) == NULL) {
		(void) fprintf(stderr, "%s", libzfs_error_init(errno));
		return (1);
	}

	libzfs_print_on_error(g_zfs, B_TRUE);

	zfs_save_arguments(argc, argv, history_str, sizeof (history_str));

	/*
	 * Many commands modify input strings for string parsing reasons.
	 * We create a copy to protect the original argv.
	 */
	newargv = malloc((argc + 1) * sizeof (newargv[0]));
	for (i = 0; i < argc; i++)
		newargv[i] = strdup(argv[i]);
	newargv[argc] = NULL;

	/*
	 * Run the appropriate command.
	 */
	if (find_command_idx(cmdname, &i) == 0) {
		current_command = &command_table[i];
		ret = command_table[i].func(argc - 1, newargv + 1);
	} else if (strchr(cmdname, '=')) {
		verify(find_command_idx("set", &i) == 0);
		current_command = &command_table[i];
		ret = command_table[i].func(argc, newargv);
	} else if (strcmp(cmdname, "freeze") == 0 && argc == 3) {
		/*
		 * 'freeze' is a vile debugging abomination, so we treat
		 * it as such.
		 */
		char buf[16384];
		int fd = open(ZFS_DEV, O_RDWR);
		(void) strlcpy((void *)buf, argv[2], sizeof (buf));
		return (!!ioctl(fd, ZFS_IOC_POOL_FREEZE, buf));
	} else {
		(void) fprintf(stderr, gettext("unrecognized "
		    "command '%s'\n"), cmdname);
		usage(B_FALSE);
		ret = 1;
	}

	for (i = 0; i < argc; i++)
		free(newargv[i]);
	free(newargv);

	if (ret == 0 && log_history)
		(void) zpool_log_history(g_zfs, history_str);

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
