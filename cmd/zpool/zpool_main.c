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
#include <zfs_prop.h>
#include <sys/fs/zfs.h>
#include <sys/stat.h>
#include <sys/fm/util.h>
#include <sys/fm/protocol.h>
#include <sys/zfs_ioctl.h>
#include <langinfo.h>

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
	HELP_REGUID,
	HELP_REOPEN
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
};

#define	NCOMMAND	(sizeof (command_table) / sizeof (command_table[0]))

static zpool_command_t *current_command;
static char history_str[HIS_MAX_RECORD_LEN];
static boolean_t log_history = B_TRUE;
static uint_t timestamp_fmt = NODATE;

static const char *
get_usage(zpool_help_t idx) {
	switch (idx) {
	case HELP_ADD:
		return (gettext("\tadd [-fgLnPJj] [-o property=value] "
		    "<pool> <vdev> ...\n"));
	case HELP_ATTACH:
		return (gettext("\tattach [-fjJ] [-o property=value] "
		    "<pool> <device> <new-device>\n"));
	case HELP_CLEAR:
		return (gettext("\tclear [-nF] <pool> [device]\n"));
	case HELP_CREATE:
		return (gettext("\tcreate [-fndjJ] [-o property=value] ... \n"
		    "\t    [-O file-system-property=value] ... \n"
		    "\t    [-m mountpoint] [-R root] <pool> <vdev> ...\n"));
	case HELP_DESTROY:
		return (gettext("\tdestroy [-fjJ] <pool>\n"));
	case HELP_DETACH:
		return (gettext("\tdetach [-jJ] <pool> <device>\n"));
	case HELP_EXPORT:
		return (gettext("\texport [-afjJ] <pool> ...\n"));
	case HELP_HISTORY:
		return (gettext("\thistory [-iljJ] [<pool>] ...\n"));
	case HELP_IMPORT:
		return (gettext("\timport [-jJ] [-d dir] [-D]\n"
		    "\timport [-jJ] [-d dir | -c cachefile] "
		    "[-F [-n]] <pool | id>\n"
		    "\timport [-jJ] [-o mntopts] [-o property=value] ... \n"
		    "\t    [-d dir | -c cachefile] [-D] [-f] [-m] [-N] "
		    "[-R root] [-F [-n]] -a\n"
		    "\timport [-jJ] [-o mntopts] [-o property=value] ... \n"
		    "\t    [-d dir | -c cachefile] [-D] [-f] [-m] [-N] "
		    "[-R root] [-F [-n]]\n"
		    "\t    <pool | id> [newpool]\n"));
	case HELP_IOSTAT:
		return (gettext("\tiostat [-gLPvyJj] [-T d|u] [pool] ... "
		    "[interval [count]]\n"));
	case HELP_LABELCLEAR:
		return (gettext("\tlabelclear [-f] <vdev>\n"));
	case HELP_LIST:
		return (gettext("\tlist [-gHLPvJj] [-o property[,...]] "
		    "[-T d|u] [pool] ... [interval [count]]\n"));
	case HELP_OFFLINE:
		return (gettext("\toffline [-tjJ] <pool> <device> ...\n"));
	case HELP_ONLINE:
		return (gettext("\tonline [-ejJ] <pool> <device> ...\n"));
	case HELP_REPLACE:
		return (gettext("\treplace [-fjJ] [-o property=value] "
		    "<pool> <device> [new-device]\n"));
	case HELP_REMOVE:
		return (gettext("\tremove [-jJ] <pool> <device> ...\n"));
	case HELP_REOPEN:
		return (gettext("\treopen [-jJ] <pool>\n"));
	case HELP_SCRUB:
		return (gettext("\tscrub [-sjJ] <pool> ...\n"));
	case HELP_STATUS:
		return (gettext("\tstatus [-gLPvxDJj] [-T d|u] [pool] ... "
		    "[interval [count]]\n"));
	case HELP_UPGRADE:
		return (gettext("\tupgrade [-jJ]\n"
		    "\tupgrade -v\n"
		    "\tupgrade [-jJ] [-V version] <-a | pool ...>\n"));
	case HELP_EVENTS:
		return (gettext("\tevents [-vHfc]\n"));
	case HELP_GET:
		return (gettext("\tget [-pHjJ] <\"all\" | property[,...]> "
		    "<pool> ...\n"));
	case HELP_SET:
		return (gettext("\tset [-jJ] <property=value> <pool> \n"));
	case HELP_SPLIT:
		return (gettext("\tsplit [-gLnPJj] [-R altroot] [-o mntopts]\n"
		    "\t    [-o property=value] <pool> <newpool> "
		    "[<device> ...]\n"));
	case HELP_REGUID:
		return (gettext("\treguid <pool>\n"));
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
    boolean_t poolprop, zfs_json_t *json)
{
	zpool_prop_t prop = ZPROP_INVAL;
	zfs_prop_t fprop;
	nvlist_t *proplist;
	const char *normnm;
	char *strval;
	char errbuf[1024];

	if (*props == NULL &&
	    nvlist_alloc(props, NV_UNIQUE_NAME, 0) != 0) {
		(void) snprintf(errbuf, sizeof (errbuf),
		    gettext("internal error: out of memory"));
		if (!json->json && !json->ld_json)
			fnvlist_add_string(json->nv_dict_error,
			    "error", errbuf);
		else
			fprintf(stderr, "%s\n", errbuf);
		return (1);
	}

	proplist = *props;

	if (poolprop) {
		const char *vname = zpool_prop_to_name(ZPOOL_PROP_VERSION);

		if ((prop = zpool_name_to_prop(propname)) == ZPROP_INVAL &&
		    !zpool_prop_feature(propname)) {
			(void) snprintf(errbuf, sizeof (errbuf),
			    gettext("property '%s' is "
			    "not a valid pool property"), propname);
			if (!json->json && !json->ld_json)
				fnvlist_add_string(json->nv_dict_error,
				    "error", errbuf);
			else
				fprintf(stderr, "%s\n", errbuf);
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
			(void) snprintf(errbuf, sizeof (errbuf),
			    gettext("'feature@' and "
			    "'version' properties cannot be specified "
			    "together"));
			if (!json->json && !json->ld_json)
				fnvlist_add_string(json->nv_dict_error,
			    "error", errbuf);
			else
				fprintf(stderr, "%s\n", errbuf);
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
		(void) snprintf(errbuf, sizeof (errbuf),
		    gettext("property '%s' "
		    "specified multiple times"), propname);
			if (!json->json && !json->ld_json)
				fnvlist_add_string(json->nv_dict_error,
			    "error", errbuf);
			else
				fprintf(stderr, "%s\n", errbuf);
		return (2);
	}

	if (nvlist_add_string(proplist, normnm, propval) != 0) {
		(void) snprintf(errbuf, sizeof (errbuf),
		    gettext("internal "
		    "error: out of memory"));
			if (!json->json && !json->ld_json)
				fnvlist_add_string(json->nv_dict_error,
			    "error", errbuf);
			else
				fprintf(stderr, "%s\n", errbuf);
		return (1);
	}

	return (0);
}

/*
 * Set a default property pair (name, string-value) in a property nvlist
 */
static int
add_prop_list_default(const char *propname, char *propval, nvlist_t **props,
    boolean_t poolprop, zfs_json_t *json)
{
	char *pval;

	if (nvlist_lookup_string(*props, propname, &pval) == 0)
		return (0);

	return (add_prop_list(propname, propval, props, B_TRUE, json));
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
	nvlist_t *nvroot = NULL;
	char *poolname;
	int ret;
	zpool_handle_t *zhp;
	nvlist_t *config;
	nvlist_t *props = NULL;
	char *propval;
	zfs_json_t json;
	json.json = json.ld_json = B_FALSE;
	char errbuf[1024];


	/* check options */
	while ((c = getopt(argc, argv, "JjfgLno:P")) != -1) {
		switch (c) {
		case 'j':
		case 'J':
			if (json.json)
				break;
			json.json = B_TRUE;
			json.nv_dict_props = fnvlist_alloc();
			json.nv_dict_error = fnvlist_alloc();
			json.nb_elem = 0;
			json.json_data = NULL;
			fnvlist_add_string(json.nv_dict_props,
			    "cmd", "zpool add");
			fnvlist_add_string(json.nv_dict_error, "error", "");
			break;
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
			    (add_prop_list(optarg, propval,
			    &props, B_TRUE, NULL)))
				usage(B_FALSE);
			break;
		case 'P':
			name_flags |= VDEV_NAME_PATH;
			break;
		case '?':
			(void) sprintf(errbuf, gettext("invalid option '%c'"),
			    optopt);
			if (!json.json) {
				fprintf(stderr, "%s\n", errbuf);
				usage(B_FALSE);
			} else {
				fnvlist_add_string(json.nv_dict_error,
				    "error", errbuf);
				goto json_usage;
			}

		}
	}

	argc -= optind;
	argv += optind;

	/* get pool name and check number of arguments */
	if (argc < 1) {
		(void) sprintf(errbuf, gettext("missing pool name argument"));
		if (!json.json) {
				fprintf(stderr, "%s\n", errbuf);
				usage(B_FALSE);
			} else {
				fnvlist_add_string(json.nv_dict_error,
				    "error", errbuf);
				goto json_usage;
			}
	}
	if (argc < 2) {
		(void) sprintf(errbuf, gettext("missing vdev specification"));
		if (!json.json) {
				fprintf(stderr, "%s\n", errbuf);
				usage(B_FALSE);
		} else {
			fnvlist_add_string(json.nv_dict_error, "error", errbuf);
			goto json_usage;
		}
	}

	poolname = argv[0];

	argc--;
	argv++;

	if ((zhp = zpool_open(&json, g_zfs, poolname)) == NULL) {
		ret = 1;
		goto json_out;
	}

	if ((config = zpool_get_config(zhp, NULL)) == NULL) {
		(void) sprintf(errbuf, gettext("pool '%s' is unavailable"),
		    poolname);
		if (!json.json)
			fprintf(stderr, "%s\n", errbuf);
		else
			fnvlist_add_string(json.nv_dict_error, "error", errbuf);
		zpool_close(zhp);
		ret = 1;
		goto json_out;
	}

	/* pass off to get_vdev_spec for processing */
	nvroot = make_root_vdev(zhp, props, force, !force, B_FALSE, dryrun,
	    argc, argv, &json);
	if (nvroot == NULL) {
		zpool_close(zhp);
		ret = 1;
		goto json_out;
	}

	if (dryrun && !json.json) {
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
		ret = (zpool_add(zhp, nvroot, &json) != 0);
	}

	nvlist_free(props);
	nvlist_free(nvroot);
	zpool_close(zhp);

json_out:
	if (json.json) {
		fnvlist_add_string(json.nv_dict_props, "schema_version", "1.0");
		fnvlist_add_nvlist_array(json.nv_dict_props, "stdout",
		    (nvlist_t **)json.json_data, json.nb_elem);
		fnvlist_add_nvlist(json.nv_dict_props,
		    "stderr", json.nv_dict_error);
		nvlist_print_json(stdout, json.nv_dict_props);
		fprintf(stdout, "\n");
		fflush(stdout);
		while (((json.nb_elem)--) > 0)
			fnvlist_free(
			    ((nvlist_t **)
			    (json.json_data))[json.nb_elem]);
		free(json.json_data);
		fnvlist_free(json.nv_dict_error);
		fnvlist_free(json.nv_dict_props);
	}
	return (ret);

json_usage:
	if (json.json) {
		fnvlist_add_string(json.nv_dict_props, "schema_version", "1.0");
		fnvlist_add_nvlist_array(json.nv_dict_props, "stdout",
		    (nvlist_t **)json.json_data, json.nb_elem);
		fnvlist_add_nvlist(json.nv_dict_props,
		    "stderr", json.nv_dict_error);
		nvlist_print_json(stdout, json.nv_dict_props);
		fprintf(stdout, "\n");
		fflush(stdout);
		while (((json.nb_elem)--) > 0)
			fnvlist_free(
			    ((nvlist_t **)
			    (json.json_data))[json.nb_elem]);
		free(json.json_data);
		fnvlist_free(json.nv_dict_error);
		fnvlist_free(json.nv_dict_props);
	}
	exit(2);
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
	int c;
	zfs_json_t json;
	json.ld_json = json.json = B_FALSE;
	char errbuf[1024];

	while ((c = getopt(argc, argv, "Jj")) != -1) {
		switch (c) {
		case 'j':
		case 'J':
			if (json.json)
				break;
			json.json = B_TRUE;
			json.nv_dict_props = fnvlist_alloc();
			json.nv_dict_error = fnvlist_alloc();
			json.nb_elem = 0;
			json.json_data = NULL;
			fnvlist_add_string(json.nv_dict_props,
			    "cmd", "zpool remove");
			fnvlist_add_string(json.nv_dict_error, "error", "");
			break;
		}
	}
	argc -= optind;
	argv += optind;
	/* get pool name and check number of arguments */
	if (argc < 1) {
		(void) sprintf(errbuf, gettext("missing pool name argument"));
		if (json.json) {
			fnvlist_add_string(json.nv_dict_error, "error", errbuf);
			goto json_usage;
		} else {
			fprintf(stderr, "%s\n", errbuf);
			usage(B_FALSE);
		}
	}
	if (argc < 2) {
		(void) sprintf(errbuf, gettext("missing device"));
		if (json.json) {
			fnvlist_add_string(json.nv_dict_error, "error", errbuf);
			goto json_usage;
		} else {
			fprintf(stderr, "%s\n", errbuf);
			usage(B_FALSE);
		}
	}

	poolname = argv[0];

	if ((zhp = zpool_open(&json, g_zfs, poolname)) == NULL) {
		ret = 1;
		goto json_out;
	}

	for (i = 1; i < argc; i++) {
		if (zpool_vdev_remove(zhp, argv[i], &json) != 0)
			ret = 1;
	}

	zpool_close(zhp);
json_out:
	if (json.json) {
		fnvlist_add_string(json.nv_dict_props, "schema_version", "1.0");
		fnvlist_add_nvlist_array(json.nv_dict_props, "stdout",
		    (nvlist_t **)json.json_data, json.nb_elem);
		fnvlist_add_nvlist(json.nv_dict_props,
		    "stderr", json.nv_dict_error);
		nvlist_print_json(stdout, json.nv_dict_props);
		fprintf(stdout, "\n");
		fflush(stdout);
		while (((json.nb_elem)--) > 0)
			fnvlist_free(
			    ((nvlist_t **)
			    (json.json_data))[json.nb_elem]);
		free(json.json_data);
		fnvlist_free(json.nv_dict_error);
		fnvlist_free(json.nv_dict_props);
	}
	return (ret);

json_usage:
	if (json.json) {
		fnvlist_add_string(json.nv_dict_props, "schema_version", "1.0");
		fnvlist_add_nvlist_array(json.nv_dict_props, "stdout",
		    (nvlist_t **)json.json_data, json.nb_elem);
		fnvlist_add_nvlist(json.nv_dict_props,
		    "stderr", json.nv_dict_error);
		nvlist_print_json(stdout, json.nv_dict_props);
		fprintf(stdout, "\n");
		fflush(stdout);
		while (((json.nb_elem)--) > 0)
			fnvlist_free(
			    ((nvlist_t **)
			    (json.json_data))[json.nb_elem]);
		free(json.json_data);
		fnvlist_free(json.nv_dict_error);
		fnvlist_free(json.nv_dict_props);
	}
	exit(2);
}

/*
 * zpool labelclear <vdev>
 *
 * Verifies that the vdev is not active and zeros out the label information
 * on the device.
 */
int
zpool_do_labelclear(int argc, char **argv)
{
	char *vdev, *name;
	int c, fd = -1, ret = 0;
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
		(void) fprintf(stderr, gettext("missing vdev device name\n"));
		usage(B_FALSE);
	}

	vdev = argv[0];
	if ((fd = open(vdev, O_RDWR)) < 0) {
		(void) fprintf(stderr, gettext("Unable to open %s\n"), vdev);
		return (B_FALSE);
	}

	name = NULL;
	if (zpool_in_use(g_zfs, fd, &state, &name, &inuse, NULL) != 0) {
		if (force)
			goto wipe_label;

		(void) fprintf(stderr,
		    gettext("Unable to determine pool state for %s\n"
		    "Use -f to force the clearing any label data\n"), vdev);

		return (1);
	}

	if (inuse) {
		switch (state) {
		default:
		case POOL_STATE_ACTIVE:
		case POOL_STATE_SPARE:
		case POOL_STATE_L2CACHE:
			(void) fprintf(stderr,
			    gettext("labelclear operation failed.\n"
			    "\tVdev %s is a member (%s), of pool \"%s\".\n"
			    "\tTo remove label information from this device, "
			    "export or destroy\n\tthe pool, or remove %s from "
			    "the configuration of this pool\n\tand retry the "
			    "labelclear operation.\n"),
			    vdev, zpool_pool_state_to_name(state), name, vdev);
			ret = 1;
			goto errout;

		case POOL_STATE_EXPORTED:
			if (force)
				break;

			(void) fprintf(stderr,
			    gettext("labelclear operation failed.\n\tVdev "
			    "%s is a member of the exported pool \"%s\".\n"
			    "\tUse \"zpool labelclear -f %s\" to force the "
			    "removal of label\n\tinformation.\n"),
			    vdev, name, vdev);
			ret = 1;
			goto errout;

		case POOL_STATE_POTENTIALLY_ACTIVE:
			if (force)
				break;

			(void) fprintf(stderr,
			    gettext("labelclear operation failed.\n"
			    "\tVdev %s is a member of the pool \"%s\".\n"
			    "\tThis pool is unknown to this system, but may "
			    "be active on\n\tanother system. Use "
			    "\'zpool labelclear -f %s\' to force the\n"
			    "\tremoval of label information.\n"),
			    vdev, name, vdev);
			ret = 1;
			goto errout;

		case POOL_STATE_DESTROYED:
			/* inuse should never be set for a destroyed pool... */
			break;
		}
	}

wipe_label:
	if (zpool_clear_label(fd) != 0) {
		(void) fprintf(stderr,
		    gettext("Label clear failed on vdev %s\n"), vdev);
		ret = 1;
	}

errout:
	close(fd);
	if (name != NULL)
		free(name);

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
	zfs_json_t	json;
	json.json = json.ld_json = B_FALSE;
	char errbuf[1024];

	/* check options */
	while ((c = getopt(argc, argv, ":JjfndR:m:o:O:t:")) != -1) {
		switch (c) {
		case 'j':
		case 'J':
			if (json.json)
				break;
			json.json = B_TRUE;
			json.nv_dict_props = fnvlist_alloc();
			json.nv_dict_error = fnvlist_alloc();
			json.nb_elem = 0;
			json.json_data = NULL;
			fnvlist_add_string(json.nv_dict_props,
			    "cmd", "zpool create");
			fnvlist_add_string(json.nv_dict_error, "error", "");
			break;
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
			    ZPOOL_PROP_ALTROOT), optarg, &props, B_TRUE, NULL))
				goto errout;
			if (add_prop_list_default(zpool_prop_to_name(
			    ZPOOL_PROP_CACHEFILE), "none",
			    &props, B_TRUE, NULL))
				goto errout;
			break;
		case 'm':
			/* Equivalent to -O mountpoint=optarg */
			mountpoint = optarg;
			break;
		case 'o':
			if ((propval = strchr(optarg, '=')) == NULL) {
				(void) snprintf(errbuf,
				    sizeof (errbuf),
				    gettext("missing "
				    "'=' for -o option"));
					if (json.json)
						fnvlist_add_string(
						    json.nv_dict_error,
						    "error", errbuf);
					else
						fprintf(stderr, "%s\n", errbuf);
				goto errout;
			}
			*propval = '\0';
			propval++;

			if (add_prop_list(optarg, propval,
			    &props, B_TRUE, NULL))
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
			break;
		case 'O':
			if ((propval = strchr(optarg, '=')) == NULL) {
				(void) snprintf(errbuf,
				    sizeof (errbuf),
				    gettext("missing "
				    "'=' for -O option"));
				if (json.json)
					fnvlist_add_string(json.nv_dict_error,
					    "error", errbuf);
				else
					fprintf(stderr, "%s\n", errbuf);
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
			    B_FALSE, NULL)) {
				goto errout;
			}
			break;
		case 't':
			/*
			 * Sanity check temporary pool name.
			 */
			if (strchr(optarg, '/') != NULL) {
				if (!json.json) {
					(void) fprintf(stderr,
					    gettext("cannot create "
					    "'%s': invalid character "
					    "'/' in temporary "
					    "name\n"), optarg);
					(void) fprintf(stderr,
					    gettext("use 'zfs "
					    "create' to create a dataset\n"));
				} else {
					(void) snprintf(errbuf,
					    sizeof (errbuf),
					    gettext("cannot create "
					    "'%s': invalid character"
					    " '/' in temporary "
					    "name, use 'zfs "
					    "create' to create a dataset"),
					    optarg);
					fnvlist_add_string(json.nv_dict_error,
					    "error", errbuf);
				}
				goto errout;
			}

			if (add_prop_list(zpool_prop_to_name(
			    ZPOOL_PROP_TNAME), optarg, &props, B_TRUE, NULL))
				goto errout;
			if (add_prop_list_default(zpool_prop_to_name(
			    ZPOOL_PROP_CACHEFILE), "none",
			    &props, B_TRUE, NULL))
				goto errout;
			tname = optarg;
			break;
		case ':':
			(void) snprintf(errbuf,
			    sizeof (errbuf),
			    gettext("missing argument for "
			    "'%c' option"), optopt);
			if (!json.json)
				fprintf(stderr, "%s\n", errbuf);
			else
				fnvlist_add_string(json.nv_dict_error,
				    "error", errbuf);
			goto badusage;
		case '?':
			(void) snprintf(errbuf,
			    sizeof (errbuf),
			    gettext("invalid option '%c'"),
			    optopt);
			if (!json.json)
				fprintf(stderr, "%s\n", errbuf);
			else
				fnvlist_add_string(json.nv_dict_error,
				    "error", errbuf);
			goto badusage;
		}
	}

	argc -= optind;
	argv += optind;

	/* get pool name and check number of arguments */
	if (argc < 1) {
		(void) snprintf(errbuf,
		    sizeof (errbuf),
		    gettext("missing pool name argument"));
		if (!json.json)
			fprintf(stderr, "%s\n", errbuf);
		else
			fnvlist_add_string(json.nv_dict_error,
			    "error", errbuf);
		goto badusage;
	}
	if (argc < 2) {
		(void) snprintf(errbuf,
		    sizeof (errbuf),
		    gettext("missing vdev specification"));
		if (!json.json)
			fprintf(stderr, "%s\n", errbuf);
		else
			fnvlist_add_string(json.nv_dict_error,
			    "error", errbuf);
		goto badusage;
	}

	poolname = argv[0];

	/*
	 * As a special case, check for use of '/' in the name, and direct the
	 * user to use 'zfs create' instead.
	 */
	if (strchr(poolname, '/') != NULL) {
		if (!json.json) {
			(void) fprintf(stderr,
			    gettext("cannot create '%s': invalid "
			    "character '/' in pool name\n"), poolname);
			(void) fprintf(stderr, gettext("use 'zfs create' to "
			    "create a dataset\n"));
		} else {
			(void) snprintf(errbuf,
			    sizeof (errbuf),
			    gettext("cannot create '%s': invalid "
			    "character '/' in pool name,"
			    " use 'zfs create' to "
			    "create a dataset"), poolname);
			fnvlist_add_string(json.nv_dict_error, "error", errbuf);
		}
		goto errout;
	}

	/* pass off to get_vdev_spec for bulk processing */
	nvroot = make_root_vdev(NULL, props, force, !force, B_FALSE, dryrun,
	    argc - 1, argv + 1, &json);
	if (nvroot == NULL)
		goto errout;

	/* make_root_vdev() allows 0 toplevel children if there are spares */
	if (!zfs_allocatable_devs(nvroot)) {
		(void) snprintf(errbuf,
		    sizeof (errbuf),
		    gettext("invalid vdev "
		    "specification: at least one toplevel vdev must be "
		    "specified"));
		if (!json.json)
			fprintf(stderr, "%s\n", errbuf);
		else
			fnvlist_add_string(json.nv_dict_error, "error", errbuf);
		goto errout;
	}

	if (altroot != NULL && altroot[0] != '/') {
		(void) snprintf(errbuf,
		    sizeof (errbuf),
		    gettext("invalid alternate root '%s': "
		    "must be an absolute path"), altroot);
		if (!json.json)
			fprintf(stderr, "%s\n", errbuf);
		else
			fnvlist_add_string(json.nv_dict_error, "error", errbuf);
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
			(void) snprintf(errbuf,
			    sizeof (errbuf),
			    gettext("invalid mountpoint "
			    "'%s': must be an absolute "
			    "path, 'legacy', or "
			    "'none'"), mountpoint);
			if (!json.json)
				fprintf(stderr,
				    "%s\n", errbuf);
			else
				fnvlist_add_string(json.nv_dict_error,
				    "error", errbuf);
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
			if (!json.json) {
				(void) fprintf(stderr,
				    gettext("mountpoint '%s' : "
				    "%s\n"), buf, strerror(errno));
				(void) fprintf(stderr, gettext("use '-m' "
				    "option to provide a different default\n"));
			} else {
				(void) snprintf(errbuf,
				    sizeof (errbuf),
				    gettext("mountpoint '%s' : "
				    "%s, use '-m' option to provide "
				    "a different default"),
				    buf, strerror(errno));
				fnvlist_add_string(json.nv_dict_error,
				    "error", errbuf);
			}
			goto errout;
		} else if (dirp) {
			int count = 0;

			while (count < 3 && readdir(dirp) != NULL)
				count++;
			(void) closedir(dirp);

			if (count > 2) {
				if (!json.json) {
					(void) fprintf(stderr,
					    gettext("mountpoint "
					    "'%s' exists and is"
					    " not empty\n"), buf);
					(void) fprintf(stderr,
					    gettext("use '-m' "
					    "option to provide a "
					    "different default\n"));
				} else {
					(void) snprintf(errbuf,
					    sizeof (errbuf),
					    gettext("mountpoint "
						    "'%s' exists and is not"
						    " empty, use '-m' "
						    "option to provide a "
						    "different default "), buf);
					fnvlist_add_string(json.nv_dict_error,
					    "error", errbuf);
				}
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
		    mountpoint, &fsprops, B_FALSE, NULL);
		if (ret != 0)
			goto errout;
	}

	ret = 1;
	if (dryrun && !json.json) {
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
		if (enable_all_pool_feat) {
			spa_feature_t i;
			for (i = 0; i < SPA_FEATURES; i++) {
				char propname[MAXPATHLEN];
				zfeature_info_t *feat = &spa_feature_table[i];

				(void) snprintf(propname, sizeof (propname),
				    "feature@%s", feat->fi_uname);

				/*
				 * Skip feature if user specified it manually
				 * on the command line.
				 */
				if (nvlist_exists(props, propname))
					continue;

				ret = add_prop_list(propname,
				    ZFS_FEATURE_ENABLED, &props, B_TRUE, NULL);
				if (ret != 0)
					goto errout;
			}
		}

		ret = 1;
		if (zpool_create(g_zfs, poolname,
		    nvroot, props, fsprops, &json) == 0) {
			zfs_handle_t *pool = zfs_open(&json, g_zfs,
			    tname ? tname : poolname, ZFS_TYPE_FILESYSTEM);
			if (pool != NULL) {
				if (zfs_mount(NULL, pool, NULL, 0) == 0)
					ret = zfs_shareall(pool, NULL);
				zfs_close(pool);
			}
		} else if (libzfs_errno(g_zfs) == EZFS_INVALIDNAME) {
			(void) snprintf(errbuf,
			    sizeof (errbuf),
			    gettext("pool name may have "
			    "been omitted"));
			if (!json.json)
				fprintf(stderr, "%s\n", errbuf);
			else
				fnvlist_add_string(json.nv_dict_error,
				    "error", errbuf);
		}
	}
errout:
if (json.json) {
		fnvlist_add_string(json.nv_dict_props, "schema_version", "1.0");
		fnvlist_add_nvlist_array(json.nv_dict_props, "stdout",
		    (nvlist_t **)json.json_data, json.nb_elem);
		fnvlist_add_nvlist(json.nv_dict_props,
		    "stderr", json.nv_dict_error);
		nvlist_print_json(stdout, json.nv_dict_props);
		fprintf(stdout, "\n");
		fflush(stdout);
		while (((json.nb_elem)--) > 0)
			fnvlist_free(
			    ((nvlist_t **)
			    (json.json_data))[json.nb_elem]);
		free(json.json_data);
		fnvlist_free(json.nv_dict_error);
		fnvlist_free(json.nv_dict_props);
	}
	nvlist_free(nvroot);
	nvlist_free(fsprops);
	nvlist_free(props);
	return (ret);
badusage:
if (json.json) {
		fnvlist_add_string(json.nv_dict_props, "schema_version", "1.0");
		fnvlist_add_nvlist_array(json.nv_dict_props, "stdout",
		    (nvlist_t **)json.json_data, json.nb_elem);
		fnvlist_add_nvlist(json.nv_dict_props,
		    "stderr", json.nv_dict_error);
		nvlist_print_json(stdout, json.nv_dict_props);
		fprintf(stdout, "\n");
		fflush(stdout);
		while (((json.nb_elem)--) > 0)
			fnvlist_free(
			    ((nvlist_t **)
			    (json.json_data))[json.nb_elem]);
		free(json.json_data);
		fnvlist_free(json.nv_dict_error);
		fnvlist_free(json.nv_dict_props);
	}
	nvlist_free(fsprops);
	nvlist_free(props);
	if (!json.json)
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
	zfs_json_t json;
	json.json = json.ld_json = B_FALSE;
	char errbuf[1024];

	/* check options */
	while ((c = getopt(argc, argv, "Jjf")) != -1) {
		switch (c) {
		case 'j':
		case 'J':
			if (json.json)
				break;
			json.json = B_TRUE;
			json.nv_dict_props = fnvlist_alloc();
			json.nv_dict_error = fnvlist_alloc();
			json.nb_elem = 0;
			json.json_data = NULL;
			fnvlist_add_string(json.nv_dict_props,
			    "cmd", "zpool destroy");
			fnvlist_add_string(json.nv_dict_error, "error", "");
			break;
		case 'f':
			force = B_TRUE;
			break;
		case '?':
			(void) snprintf(errbuf,
			    sizeof (errbuf),
			    gettext("invalid option '%c'"),
			    optopt);
			if (!json.json) {
				fprintf(stderr, "%s\n", errbuf);
				usage(B_FALSE);
			} else {
				fnvlist_add_string(json.nv_dict_error,
				    "error", errbuf);
				goto json_usage;
			}
		}
	}

	argc -= optind;
	argv += optind;

	/* check arguments */
	if (argc < 1) {
		(void) snprintf(errbuf,
		    sizeof (errbuf),
		    gettext("missing pool argument"));
		if (!json.json) {
			fprintf(stderr, "%s\n", errbuf);
				usage(B_FALSE);
		} else {
			fnvlist_add_string(json.nv_dict_error, "error", errbuf);
			goto json_usage;
		}
	}
	if (argc > 1) {
		(void) snprintf(errbuf,
		    sizeof (errbuf),
		    gettext("too many arguments"));
		if (!json.json) {
			fprintf(stderr, "%s\n", errbuf);
				usage(B_FALSE);
		} else {
			fnvlist_add_string(json.nv_dict_error, "error", errbuf);
			goto json_usage;
		}
	}

	pool = argv[0];

	if ((zhp = zpool_open_canfail(&json, g_zfs, pool)) == NULL) {
		/*
		 * As a special case, check for use of '/' in the name, and
		 * direct the user to use 'zfs destroy' instead.
		 */
		if (strchr(pool, '/') != NULL) {
			(void) snprintf(errbuf,
			    sizeof (errbuf),
			    gettext("use 'zfs destroy' to "
			    "destroy a dataset"));
			if (!json.json)
				fprintf(stderr, "%s\n", errbuf);
			else
			fnvlist_add_string(json.nv_dict_error,
			    "error", errbuf);
		}
		ret = 1;
		goto out;
	}

	if (zpool_disable_datasets(zhp, force, &json) != 0) {
		(void) snprintf(errbuf,
		    sizeof (errbuf),
		    gettext("could not destroy '%s': "
		    "could not unmount datasets"), zpool_get_name(zhp));
		if (!json.json)
			fprintf(stderr, "%s\n", errbuf);
		else
			fnvlist_add_string(json.nv_dict_error, "error", errbuf);
		ret = 1;
		goto out;
	}

	/* The history must be logged as part of the export */
	log_history = B_FALSE;

	ret = (zpool_destroy(zhp, history_str, &json) != 0);

	zpool_close(zhp);
out:
	if (json.json) {
		fnvlist_add_string(json.nv_dict_props, "schema_version", "1.0");
		fnvlist_add_nvlist_array(json.nv_dict_props, "stdout",
		    (nvlist_t **)json.json_data, json.nb_elem);
		fnvlist_add_nvlist(json.nv_dict_props,
		    "stderr", json.nv_dict_error);
		nvlist_print_json(stdout, json.nv_dict_props);
		fprintf(stdout, "\n");
		fflush(stdout);
		while (((json.nb_elem)--) > 0)
			fnvlist_free(
			    ((nvlist_t **)
			    (json.json_data))[json.nb_elem]);
		free(json.json_data);
		fnvlist_free(json.nv_dict_error);
		fnvlist_free(json.nv_dict_props);
	}

	return (ret);

json_usage:
	if (json.json) {
		fnvlist_add_string(json.nv_dict_props, "schema_version", "1.0");
		fnvlist_add_nvlist_array(json.nv_dict_props, "stdout",
		    (nvlist_t **)json.json_data, json.nb_elem);
		fnvlist_add_nvlist(json.nv_dict_props,
		    "stderr", json.nv_dict_error);
		nvlist_print_json(stdout, json.nv_dict_props);
		fprintf(stdout, "\n");
		fflush(stdout);
		while (((json.nb_elem)--) > 0)
			fnvlist_free(
			    ((nvlist_t **)
			    (json.json_data))[json.nb_elem]);
		free(json.json_data);
		fnvlist_free(json.nv_dict_error);
		fnvlist_free(json.nv_dict_props);
	}
	exit(2);


}

typedef struct export_cbdata {
	boolean_t force;
	boolean_t hardforce;
} export_cbdata_t;

/*
 * Export one pool
 */
int
zpool_export_one(zpool_handle_t *zhp, void *data, zfs_json_t *json)
{
	export_cbdata_t *cb = data;

	if (zpool_disable_datasets(zhp, cb->force, json) != 0)
		return (1);

	/* The history must be logged as part of the export */
	log_history = B_FALSE;

	if (cb->hardforce) {
		if (zpool_export_force(zhp, history_str, json) != 0)
			return (1);
	} else if (zpool_export(zhp, cb->force, history_str, json) != 0) {
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
	zfs_json_t json;
	json.json = json.ld_json = B_FALSE;
	char errbuf[1024];

	/* check options */
	while ((c = getopt(argc, argv, "JjafF")) != -1) {
		switch (c) {
		case 'j':
		case 'J':
			if (json.json)
				break;
			json.json = B_TRUE;
			json.nv_dict_props = fnvlist_alloc();
			json.nv_dict_error = fnvlist_alloc();
			json.json_data = NULL;
			json.nb_elem = 0;
			fnvlist_add_string(json.nv_dict_props,
			    "cmd", "zpool export");
			fnvlist_add_string(json.nv_dict_props,
			    "schema_version", "1.0");
			break;
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
			(void) snprintf(errbuf, sizeof (errbuf),
			    gettext("invalid option '%c'"), optopt);
			if (json.json)
				fnvlist_add_string(json.nv_dict_error, "error",
				    errbuf);
			else
				fprintf(stderr, "%s\n", errbuf);
			goto usage;
		}
	}

	cb.force = force;
	cb.hardforce = hardforce;
	argc -= optind;
	argv += optind;

	if (do_all) {
		if (argc != 0) {
			(void) snprintf(errbuf, sizeof (errbuf),
			    gettext("too many arguments"));
			if (json.json)
				fnvlist_add_string(json.nv_dict_error,
				    "error", errbuf);
			else
				fprintf(stderr, "%s\n", errbuf);
			goto usage;
		}

		ret = for_each_pool(argc, argv, B_TRUE, NULL,
		    zpool_export_one, &cb, &json);
		goto json_out;
	}

	/* check arguments */
	if (argc < 1) {
		(void) snprintf(errbuf, sizeof (errbuf),
		    gettext("missing pool argument"));
			if (json.json)
				fnvlist_add_string(json.nv_dict_error, "error",
				    errbuf);
			else
				fprintf(stderr, "%s\n", errbuf);
			goto usage;
	}

	ret = for_each_pool(argc, argv,
	    B_TRUE, NULL, zpool_export_one, &cb, &json);

json_out:
	if (json.json) {
		fnvlist_add_nvlist_array(json.nv_dict_props, "stdout",
		    (nvlist_t **)json.json_data, json.nb_elem);
		fnvlist_add_nvlist(json.nv_dict_props,
		    "stderr", json.nv_dict_error);
		nvlist_print_json(stdout, json.nv_dict_props);
		fnvlist_free(json.nv_dict_error);
		fnvlist_free(json.nv_dict_props);
		printf("\n");
		fflush(stdout);
	}
	return (ret);

	usage :
		if (json.json) {
			fnvlist_add_nvlist_array(json.nv_dict_props, "stdout",
			    (nvlist_t **)json.json_data, json.nb_elem);
			fnvlist_add_nvlist(json.nv_dict_props,
			    "stderr", json.nv_dict_error);
			nvlist_print_json(stdout, json.nv_dict_props);
			fnvlist_free(json.nv_dict_error);
			fnvlist_free(json.nv_dict_props);
			printf("\n");
			fflush(stdout);
		} else
			usage(B_FALSE);
		exit(2);
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

	name = zpool_vdev_name(g_zfs, zhp, nv, name_flags | VDEV_NAME_TYPE_ID);
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
find_spare(zpool_handle_t *zhp, void *data, zfs_json_t *json)
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
static void
print_status_config(zpool_handle_t *zhp, const char *name, nvlist_t *nv,
    int namewidth, int depth, boolean_t isspare, int name_flags,
    zfs_json_t *json, nvlist_t *buffer_t)
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
	nvlist_t *buffer_t2;
	void *data_buf;


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
	if (!json || (!json->json && !json->ld_json))
		(void) printf("\t%*s%-*s  %-8s", depth, "", namewidth - depth,
		    name, state);
	if (!isspare) {
			zfs_nicenum(vs->vs_read_errors,
			    rbuf, sizeof (rbuf));
			zfs_nicenum(vs->vs_write_errors,
			    wbuf, sizeof (wbuf));
			zfs_nicenum(vs->vs_checksum_errors,
			    cbuf, sizeof (cbuf));
		if (!json || (!json->json && !json->ld_json)) {
			(void) printf(" %5s %5s %5s", rbuf, wbuf, cbuf);
		} else {
		    fnvlist_add_string(buffer_t,
			    "NAME", name);
			fnvlist_add_string(buffer_t,
			    "STATE", state);
			fnvlist_add_string(buffer_t,
			    "READ", rbuf);
			fnvlist_add_string(buffer_t,
			    "WRITE", wbuf);
			fnvlist_add_string(buffer_t,
			    "CHSUM", cbuf);
		}
	}
	if (nvlist_lookup_uint64(nv, ZPOOL_CONFIG_NOT_PRESENT,
	    &notpresent) == 0) {
		char *path;
		verify(nvlist_lookup_string(nv, ZPOOL_CONFIG_PATH, &path) == 0);
		if (!json || (!json->json && !json->ld_json))
			(void) printf("  was %s", path);
		else
			fnvlist_add_string(buffer_t, "was", path);
	} else if (vs->vs_aux != 0) {
		if (!json || (!json->json && !json->ld_json))
			(void) printf("  ");

		switch (vs->vs_aux) {
		case VDEV_AUX_OPEN_FAILED:
			if (!json || (!json->json && !json->ld_json))
				(void) printf(gettext("cannot open"));
			else
				fnvlist_add_string(buffer_t,
				    "reason", "cannot open");
			break;

		case VDEV_AUX_BAD_GUID_SUM:
			if (!json || (!json->json && !json->ld_json))
				(void) printf(gettext("missing device"));
			else
				fnvlist_add_string(buffer_t,
				    "reason", "missing device");
			break;

		case VDEV_AUX_NO_REPLICAS:
			if (!json || (!json->json && !json->ld_json))
				(void) printf(gettext("insufficient replicas"));
			else
				fnvlist_add_string(buffer_t,
				    "reason", "insufficient replicas");
			break;

		case VDEV_AUX_VERSION_NEWER:
			if (!json || (!json->json && !json->ld_json))
				(void) printf(gettext("newer version"));
			else
				fnvlist_add_string(buffer_t,
				    "reason", "newer version");
			break;

		case VDEV_AUX_UNSUP_FEAT:
			if (!json || (!json->json && !json->ld_json))
				(void) printf(gettext(
				    "unsupported feature(s)"));
			else
				fnvlist_add_string(buffer_t,
				    "reason", "unsupported feature(s)");
			break;

		case VDEV_AUX_SPARED:
			if (json && (json->json || json->ld_json))
				break;
			verify(nvlist_lookup_uint64(nv, ZPOOL_CONFIG_GUID,
			    &cb.cb_guid) == 0);
			if (zpool_iter(g_zfs, find_spare, &cb, NULL) == 1) {
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
			if (!json || (!json->json && !json->ld_json))
				(void) printf(gettext("too many errors"));
			else
				fnvlist_add_string(buffer_t,
				    "reason", "too many errors");
			break;

		case VDEV_AUX_IO_FAILURE:
			if (!json || (!json->json && !json->ld_json))
				(void) printf(gettext("experienced I/O "
				    "failures"));
			else
				fnvlist_add_string(buffer_t,
				    "reason", "experienced I/O failures");
			break;

		case VDEV_AUX_BAD_LOG:
			if (!json || (!json->json && !json->ld_json))
				(void) printf(gettext("bad intent log"));
			else
				fnvlist_add_string(buffer_t,
				    "reason", "bad intent log");
			break;

		case VDEV_AUX_EXTERNAL:
			if (!json || (!json->json && !json->ld_json))
				(void) printf(gettext("external device fault"));
			else
				fnvlist_add_string(buffer_t,
				    "reason", "external device fault");
			break;

		case VDEV_AUX_SPLIT_POOL:
			if (!json || (!json->json && !json->ld_json))
				(void) printf(gettext("split into new pool"));
			else
				fnvlist_add_string(buffer_t,
				    "reason", "split into new pool");
			break;

		default:
			if (!json || (!json->json && !json->ld_json))
				(void) printf(gettext("corrupted data"));
			else
				fnvlist_add_string(buffer_t,
				    "reason", "corrupted data");
			break;
		}
	}
	(void) nvlist_lookup_uint64_array(nv, ZPOOL_CONFIG_SCAN_STATS,
	    (uint64_t **)&ps, &c);
	if (ps && ps->pss_state == DSS_SCANNING &&
	    vs->vs_scan_processed != 0 && children == 0) {
		if (!json || (!json->json && !json->ld_json))
			(void) printf(gettext("  (%s)"),
			    (ps->pss_func == POOL_SCAN_RESILVER) ?
			    "resilvering" : "repairing");
	}
	if (!json || (!json->json && !json->ld_json))
		(void) printf("\n");
	data_buf = NULL;
	buffer_t2 = NULL;
	for (c = 0; c < children; c++) {
		uint64_t islog = B_FALSE, ishole = B_FALSE;
		/* Don't print logs or holes here */
		(void) nvlist_lookup_uint64(child[c], ZPOOL_CONFIG_IS_LOG,
		    &islog);
		(void) nvlist_lookup_uint64(child[c], ZPOOL_CONFIG_IS_HOLE,
		    &ishole);
		if (islog || ishole)
			continue;
		if (json && (json->json || json->ld_json)) {
			buffer_t2 = fnvlist_alloc();
			data_buf = realloc(data_buf,
			    sizeof (nvlist_t *) * (c + 1));
		}
		vname = zpool_vdev_name(g_zfs, zhp, child[c],
		    name_flags | VDEV_NAME_TYPE_ID);
		print_status_config(zhp, vname, child[c],
		    namewidth, depth + 2, isspare, name_flags,
		    json, buffer_t2);
		free(vname);
		if (json && (json->json || json->ld_json))
			((nvlist_t **)data_buf)[c] = buffer_t2;
	}
	if (json && (json->json || json->ld_json) && c > 0) {
		fnvlist_add_nvlist_array(buffer_t, "devices", data_buf, c);
		while ((c--) > 0) {
			fnvlist_free(((nvlist_t **)data_buf)[c]);
		}
		free(data_buf);
	}
}

/*
 * Print the configuration of an exported pool.  Iterate over all vdevs in the
 * pool, printing out the name and status for each one.
 */
static void
print_import_config(const char *name, nvlist_t *nv, int namewidth, int depth,
    int name_flags, zfs_json_t *json, nvlist_t *buffer_t)
{
	nvlist_t **child;
	uint_t c, children, c2;
	vdev_stat_t *vs;
	char *type, *vname;
	nvlist_t *buffer_t2;
	char *state;
	void *data_buf;

	verify(nvlist_lookup_string(nv, ZPOOL_CONFIG_TYPE, &type) == 0);
	if (strcmp(type, VDEV_TYPE_MISSING) == 0 ||
	    strcmp(type, VDEV_TYPE_HOLE) == 0)
		return;
	verify(nvlist_lookup_uint64_array(nv, ZPOOL_CONFIG_VDEV_STATS,
	    (uint64_t **)&vs, &c) == 0);
	state = zpool_state_to_name(vs->vs_state, vs->vs_aux);
	if (!json || (!json->json && !json->ld_json))
		(void) printf("\t%*s%-*s  %s", depth, "",
		    namewidth - depth, name, state);
	else {
		fnvlist_add_string(buffer_t, "name", name);
		fnvlist_add_string(buffer_t, "state", state);
	}

	if (vs->vs_aux != 0) {
		if (!json || (!json->json && !json->ld_json))
			(void) printf("  ");

		switch (vs->vs_aux) {
		case VDEV_AUX_OPEN_FAILED:
			if (!json || (!json->json && !json->ld_json))
				(void) printf(gettext("cannot open"));
			else
				fnvlist_add_string(buffer_t, "error",
				    gettext("cannot open"));
			break;

		case VDEV_AUX_BAD_GUID_SUM:
			if (!json || (!json->json && !json->ld_json))
				(void) printf(gettext("missing device"));
			else
				fnvlist_add_string(buffer_t, "error",
				    gettext("missing device"));
			break;

		case VDEV_AUX_NO_REPLICAS:
			if (!json || (!json->json && !json->ld_json))
				(void) printf(gettext("insufficient replicas"));
			else
				fnvlist_add_string(buffer_t, "error",
				    gettext("insufficient replicas"));
			break;

		case VDEV_AUX_VERSION_NEWER:
			if (!json || (!json->json && !json->ld_json))
				(void) printf(gettext("newer version"));
			else
				fnvlist_add_string(buffer_t, "error",
				    gettext("newer version"));
			break;

		case VDEV_AUX_UNSUP_FEAT:
			if (!json || (!json->json && !json->ld_json))
				(void) printf(
				    gettext("unsupported feature(s)"));
			else
				fnvlist_add_string(buffer_t, "error",
				    gettext("unsupported feature(s)"));
			break;

		case VDEV_AUX_ERR_EXCEEDED:
			if (!json || (!json->json && !json->ld_json))
				(void) printf(gettext("too many errors"));
			else
				fnvlist_add_string(buffer_t, "error",
				    gettext("too many errors"));
			break;

		default:
			if (!json || (!json->json && !json->ld_json))
				(void) printf(gettext("corrupted data"));
			else
				fnvlist_add_string(buffer_t, "error",
				    gettext("corrupted data"));
			break;
		}
	}
	if (!json || (!json->json && !json->ld_json))
		(void) printf("\n");

	if (nvlist_lookup_nvlist_array(nv, ZPOOL_CONFIG_CHILDREN,
	    &child, &children) != 0)
		return;

	data_buf = NULL;
	buffer_t2 = NULL;
	for (c = 0; c < children; c++) {
		uint64_t is_log = B_FALSE;

		(void) nvlist_lookup_uint64(child[c], ZPOOL_CONFIG_IS_LOG,
		    &is_log);
		if (is_log)
			continue;

		if (json && (json->json || json->ld_json)) {
			buffer_t2 = fnvlist_alloc();
			data_buf = realloc(data_buf,
			    sizeof (nvlist_t *) * (c + 1));
		}
		vname = zpool_vdev_name(g_zfs, NULL, child[c],
		    name_flags | VDEV_NAME_TYPE_ID);
		print_import_config(vname, child[c], namewidth, depth + 2,
		    name_flags, json, buffer_t2);
		if (json && (json->json || json->ld_json))
			((nvlist_t **)data_buf)[c] = buffer_t2;
		free(vname);
	}
	if (json && (json->json || json->ld_json)) {
		fnvlist_add_nvlist_array(buffer_t, "devices", data_buf, c);
		for (c2 = 0; c2 < c; c2++)
			fnvlist_free(((nvlist_t **)data_buf)[c2]);
		free(data_buf);
	}

	if (nvlist_lookup_nvlist_array(nv, ZPOOL_CONFIG_L2CACHE,
	    &child, &children) == 0) {
		if (!json || (!json->json && !json->ld_json))
			(void) printf(gettext("\tcache\n"));
		else
			data_buf = malloc(sizeof (char *) * children);
		for (c = 0; c < children; c++) {
			vname = zpool_vdev_name(g_zfs, NULL, child[c],
			    name_flags);
			if (!json || (!json->json && !json->ld_json))
				(void) printf("\t  %s\n", vname);
			else
				((char **)data_buf)[c] = strdup(vname);
			free(vname);
		}
		if (json && (json->json || json->ld_json)) {
			fnvlist_add_nvlist_array(buffer_t,
			    "cache", data_buf, children);
			for (c = 0; c < children; c++)
				free(((char **)data_buf)[c]);
			free(data_buf);
		}
	}

	if (nvlist_lookup_nvlist_array(nv, ZPOOL_CONFIG_SPARES,
	    &child, &children) == 0) {
		if (!json || (!json->json && !json->ld_json))
			(void) printf(gettext("\tspares\n"));
		else
			data_buf = malloc(sizeof (char *) * children);
		for (c = 0; c < children; c++) {
			vname = zpool_vdev_name(g_zfs, NULL, child[c],
			    name_flags);
			if (!json || (!json->json && !json->ld_json))
				(void) printf("\t  %s\n", vname);
			else
				((char **)data_buf)[c] = strdup(vname);
			free(vname);
		}
		if (json && (json->json || json->ld_json)) {
			fnvlist_add_nvlist_array(buffer_t,
			    "spares", data_buf, children);
			for (c = 0; c < children; c++)
				free(((char **)data_buf)[c]);
			free(data_buf);
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
print_logs(zpool_handle_t *zhp, nvlist_t *nv, int namewidth, boolean_t verbose,
    int name_flags, zfs_json_t *json, nvlist_t *buffer_t)
{
	uint_t c, children;
	nvlist_t **child;
	nvlist_t *buffer_t2;

	if (nvlist_lookup_nvlist_array(nv, ZPOOL_CONFIG_CHILDREN, &child,
	    &children) != 0)
		return;

	buffer_t2 = NULL;
	if (!json || (!json->json && !json->ld_json))
		(void) printf(gettext("\tlogs\n"));
	else
		buffer_t2 = fnvlist_alloc();

	for (c = 0; c < children; c++) {
		uint64_t is_log = B_FALSE;
		char *name;

		(void) nvlist_lookup_uint64(child[c], ZPOOL_CONFIG_IS_LOG,
		    &is_log);
		if (!is_log)
			continue;
		name = zpool_vdev_name(g_zfs, zhp, child[c],
		    name_flags | VDEV_NAME_TYPE_ID);
		if (verbose)
			print_status_config(zhp, name, child[c], namewidth,
			    2, B_FALSE, name_flags, json, buffer_t2);
		else
			print_import_config(name, child[c], namewidth, 2,
			    name_flags, json, buffer_t2);
		if (json && (json->json || json->ld_json)) {
			fnvlist_add_nvlist(buffer_t, "logs", buffer_t2);
			fnvlist_free(buffer_t2);
		}
		free(name);
	}
}

/*
 * Display the status for the given pool.
 */
static void
show_import(nvlist_t *config, zfs_json_t *json)
{
	uint64_t pool_state;
	vdev_stat_t *vs;
	char *name;
	uint64_t guid;
	char *msgid;
	nvlist_t *nvroot;
	zpool_status_t reason;
	zpool_errata_t errata;
	const char *health;
	uint_t vsc;
	int namewidth;
	char *comment;
	nvlist_t *buffer_t;
	nvlist_t *config_buffer;
	char *status_buffer = NULL;
	char buffer[1024];

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

	buffer_t = config_buffer = NULL;
	if (!json || (!json->json && !json->ld_json)) {
		(void) printf(gettext("   pool: %s\n"), name);
		(void) printf(gettext("     id: %llu\n"), (u_longlong_t)guid);
		(void) printf(gettext("  state: %s"), health);
		if (pool_state == POOL_STATE_DESTROYED)
			(void) printf(gettext(" (DESTROYED)"));
		(void) printf("\n");
	} else {
		buffer_t = fnvlist_alloc();
		config_buffer = fnvlist_alloc();
		json->nb_elem++;
		fnvlist_add_string(buffer_t, "pool", name);
		(void) snprintf(buffer, sizeof (buffer),
		    gettext("%llu"), (u_longlong_t)guid);
		fnvlist_add_string(buffer_t, "id", buffer);
		(void) snprintf(buffer, sizeof (buffer), gettext("%s"), health);
		if (pool_state == POOL_STATE_DESTROYED)
			(void) snprintf(buffer, sizeof (buffer),
			    gettext("%s %s"), buffer, "(DESTROYED)");
		fnvlist_add_string(buffer_t, "state", buffer);
	}

	switch (reason) {
	case ZPOOL_STATUS_MISSING_DEV_R:
	case ZPOOL_STATUS_MISSING_DEV_NR:
	case ZPOOL_STATUS_BAD_GUID_SUM:
		if (!json || (!json->json && !json->ld_json))
			(void) printf(gettext(" status: "
			    "One or more devices are "
			    "missing from the system.\n"));
		else
			fnvlist_add_string(buffer_t, "status",
			    "One or more devices are "
			    "missing from the system.");
		break;

	case ZPOOL_STATUS_CORRUPT_LABEL_R:
	case ZPOOL_STATUS_CORRUPT_LABEL_NR:
		if (!json || (!json->json && !json->ld_json))
			(void) printf(gettext(" status: "
			    "One or more devices contains "
			    "corrupted data.\n"));
		else
			fnvlist_add_string(buffer_t, "status",
			    "One or more devices contains "
			    "corrupted data.");
		break;

	case ZPOOL_STATUS_CORRUPT_DATA:
		if (!json || (!json->json && !json->ld_json))
			(void) printf(
			    gettext(" status: The pool data is corrupted.\n"));
		else
			fnvlist_add_string(buffer_t, "status",
			    "The pool data is corrupted.");
		break;

	case ZPOOL_STATUS_OFFLINE_DEV:
		if (!json || (!json->json && !json->ld_json))
			(void) printf(gettext(" status: One or more devices "
			    "are offlined.\n"));
		else
			fnvlist_add_string(buffer_t, "status",
			    "One or more devices "
			    "are offlined.");
		break;

	case ZPOOL_STATUS_CORRUPT_POOL:
		if (!json || (!json->json && !json->ld_json))
			(void) printf(gettext(" status: The pool metadata is "
			    "corrupted.\n"));
		else
			fnvlist_add_string(buffer_t, "status",
			    "The pool metadata is "
			    "corrupted.");
		break;

	case ZPOOL_STATUS_VERSION_OLDER:
		if (!json || (!json->json && !json->ld_json))
			(void) printf(gettext(" status: "
			    "The pool is formatted using a "
			    "legacy on-disk version.\n"));
		else
			fnvlist_add_string(buffer_t, "status",
			    "The pool is formatted using a "
			    "legacy on-disk version.");
		break;

	case ZPOOL_STATUS_VERSION_NEWER:
		if (!json || (!json->json && !json->ld_json))
			(void) printf(gettext(" status: "
			    "The pool is formatted using an "
			    "incompatible version.\n"));
		else
			fnvlist_add_string(buffer_t, "status",
			    " The pool is formatted using an "
			    "incompatible version.");
		break;

	case ZPOOL_STATUS_FEAT_DISABLED:
		if (!json || (!json->json && !json->ld_json))
			(void) printf(gettext(" status: "
			    "Some supported features are "
			    "not enabled on the pool.\n"));
		else
			fnvlist_add_string(buffer_t, "status",
			    "Some supported features are "
			    "not enabled on the pool.");
		break;

	case ZPOOL_STATUS_UNSUP_FEAT_READ:
		if (!json || (!json->json && !json->ld_json))
			(void) printf(gettext("status: "
			    "The pool uses the following "
			    "feature(s) not supported on this sytem:\n"));
		else
			status_buffer = strdup(gettext(
			    "The pool uses the following "
			    "feature(s) not supported on this sytem:"));
		zpool_print_unsup_feat(config, json, status_buffer);
		if (json && (json->json || json->ld_json)) {
			fnvlist_add_string(buffer_t, "status", status_buffer);
			free(status_buffer);
		}
		break;

	case ZPOOL_STATUS_UNSUP_FEAT_WRITE:
		if (!json || (!json->json && !json->ld_json))
			(void) printf(gettext("status: "
			    "The pool can only be accessed "
			    "in read-only mode on this system. It cannot be "
			    "accessed in read-write mode because it uses the "
			    "following feature(s) "
			    "not supported on this system:\n"));
		else
			status_buffer = strdup(gettext(
			    "The pool can only be accessed "
			    "in read-only mode on this system. It cannot be "
			    "accessed in read-write mode because it uses the "
			    "following feature(s) "
			    "not supported on this system:"));
		zpool_print_unsup_feat(config, json, status_buffer);
		if (json && (json->json || json->ld_json)) {
			fnvlist_add_string(buffer_t, "status", status_buffer);
			free(status_buffer);
		}
		break;

	case ZPOOL_STATUS_HOSTID_MISMATCH:
		if (!json || (!json->json && !json->ld_json))
			(void) printf(gettext(" status: "
			    "The pool was last accessed by "
			    "another system.\n"));
		else
			fnvlist_add_string(buffer_t, "status",
			    "The pool was last accessed by "
			    "another system.");
		break;

	case ZPOOL_STATUS_FAULTED_DEV_R:
	case ZPOOL_STATUS_FAULTED_DEV_NR:
		if (!json || (!json->json && !json->ld_json))
			(void) printf(gettext(" status: "
			    "One or more devices are "
			    "faulted.\n"));
		else
			fnvlist_add_string(buffer_t, "status",
			    "One or more devices are "
			    "faulted.");
		break;

	case ZPOOL_STATUS_BAD_LOG:
		if (!json || (!json->json && !json->ld_json))
			(void) printf(gettext(" status: "
			    "An intent log record cannot be "
			    "read.\n"));
		else
			fnvlist_add_string(buffer_t, "status",
			    "An intent log record cannot be "
			    "read.");
		break;

	case ZPOOL_STATUS_RESILVERING:
		if (!json || (!json->json && !json->ld_json))
			(void) printf(gettext(" status: "
			    "One or more devices were being "
			    "resilvered.\n"));
		else
			fnvlist_add_string(buffer_t, "status",
			    "One or more devices were being "
			    "resilvered.");
		break;

	case ZPOOL_STATUS_ERRATA:
		if (!json || (!json->json && !json->ld_json))
			(void) printf(gettext(" status: "
			    "Errata #%d detected.\n"),
			    errata);
		else {
			(void) snprintf(buffer, sizeof (buffer),
			    gettext(" status: Errata #%d detected."),
			    errata);
			fnvlist_add_string(buffer_t, "status", buffer);
		}
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
			if (!json || (!json->json && !json->ld_json))
				(void) printf(gettext(" action: "
					"The pool can be "
				    "imported using its name "
				    "or numeric identifier, "
				    "though\n\tsome features "
				    "will not be available "
				    "without an explicit 'zpool upgrade'.\n"));
			else
				fnvlist_add_string(buffer_t, "action",
				    "The pool can be "
				    "imported using its name "
				    "or numeric identifier, "
				    "though some features "
				    "will not be available "
				    "without an explicit 'zpool upgrade'.");
		} else if (reason == ZPOOL_STATUS_HOSTID_MISMATCH) {
			if (!json || (!json->json && !json->ld_json))
				(void) printf(gettext(" action: "
					"The pool can be "
				    "imported using its name or numeric "
				    "identifier and\n\tthe '-f' flag.\n"));
			else
				fnvlist_add_string(buffer_t, "action",
				    "The pool can be "
				    "imported using its "
				    "name or numeric "
				    "identifier and the '-f' flag.");
		} else if (reason == ZPOOL_STATUS_ERRATA) {
			switch (errata) {
			case ZPOOL_ERRATA_NONE:
				break;

			case ZPOOL_ERRATA_ZOL_2094_SCRUB:
				if (!json || (!json->json && !json->ld_json))
					(void) printf(gettext(" action: "
					    "The pool can "
					    "be imported using its "
					    "name or numeric "
					    "identifier,\n\thowever "
					    "there is a compat"
					    "ibility issue which "
					    "should be corrected"
					    "\n\tby running 'zpool scrub'\n"));
				else
					fnvlist_add_string(buffer_t, "action",
					    "The pool can "
					    "be imported using "
					    "its name or numeric "
					    "identifier, however "
					    "there is a compat"
					    "ibility issue which "
					    "should be corrected "
					    "by running 'zpool scrub'");
				break;

			case ZPOOL_ERRATA_ZOL_2094_ASYNC_DESTROY:
				if (!json || (!json->json && !json->ld_json))
					(void) printf(gettext(" action: "
						"The pool can"
					    "not be imported with "
					    "this version of ZFS "
					    "due to\n\tan active "
					    "asynchronous destroy. "
					    "Revert to an earlier "
					    "version\n\tand "
					    "allow the destroy "
					    "to complete before "
					    "updating.\n"));
				else
					fnvlist_add_string(buffer_t, "action",
					    "The pool can"
					    "not be imported with "
					    "this version of ZFS "
					    "due to an active "
					    "asynchronous destroy. "
					    "Revert to an earlier "
					    "version and "
					    "allow the destroy "
					    "to complete before "
					    "updating.");
				break;

			default:
				/*
				 * All errata must contain an action message.
				 */
				assert(0);
			}
		} else {
			if (!json || (!json->json && !json->ld_json))
				(void) printf(gettext(" action: "
					"The pool can be "
				    "imported using its name or numeric "
				    "identifier.\n"));
			else
				fnvlist_add_string(buffer_t, "action",
				    "The pool can be "
				    "imported using its name "
				    "or numeric identifier.");
		}
	} else if (vs->vs_state == VDEV_STATE_DEGRADED) {
		if (!json || (!json->json && !json->ld_json))
			(void) printf(gettext(" action: "
				"The pool can be imported "
			    "despite missing or damaged devices.  "
			    "The\n\tfault "
			    "tolerance of the pool may "
			    "be compromised if imported.\n"));
		else
				fnvlist_add_string(buffer_t, "action",
				    "The pool can be imported "
				    "despite missing or damaged devices.  "
				    "The fault "
				    "tolerance of the pool may be "
				    "compromised if imported.");
	} else {
		switch (reason) {
		case ZPOOL_STATUS_VERSION_NEWER:
			if (!json || (!json->json && !json->ld_json))
				(void) printf(gettext(" action: "
					"The pool cannot be "
				    "imported.  Access the pool "
				    "on a system running "
				    "newer\n\tsoftware, or recreate "
				    "the pool from backup.\n"));
			else
				fnvlist_add_string(buffer_t, "action",
				    "The pool cannot be "
				    "imported.  Access the pool "
				    "on a system running newer "
				    "software, or recreate the pool from "
				    "backup.");
			break;
		case ZPOOL_STATUS_UNSUP_FEAT_READ:
			if (!json || (!json->json && !json->ld_json))
				(void) printf(gettext("action: "
					"The pool cannot be "
				    "imported. Access the pool "
				    "on a system that supports\n\t"
				    "the required feature(s), or recreate "
				    "the pool from backup.\n"));
			else
				fnvlist_add_string(buffer_t, "action",
				    "The pool cannot be "
				    "imported. Access the pool "
				    "on a system that "
				    "supports the required feature(s), "
				    "or recreate "
				    "the pool from backup.");
			break;
		case ZPOOL_STATUS_UNSUP_FEAT_WRITE:
			if (!json || (!json->json && !json->ld_json))
				(void) printf(gettext("action: "
					"The pool cannot be "
				    "imported in read-write mode. "
				    "Import the pool with\n"
				    "\t\"-o readonly=on\", access "
				    "the pool on a system "
				    "that supports the\n\t"
				    "required feature(s), or "
				    "recreate the pool from backup.\n"));
			else
				fnvlist_add_string(buffer_t, "action",
				    "The pool cannot be "
				    "imported in read-write mode. "
				    "Import the pool with "
				    "\"-o readonly=on\", access "
				    "the pool on a system "
				    "that supports the "
				    "required feature(s), or "
				    "recreate the pool from backup.");
			break;
		case ZPOOL_STATUS_MISSING_DEV_R:
		case ZPOOL_STATUS_MISSING_DEV_NR:
		case ZPOOL_STATUS_BAD_GUID_SUM:
			if (!json || (!json->json && !json->ld_json))
				(void) printf(gettext(" action: "
					"The pool cannot be "
				    "imported. Attach the missing\n\t"
				    "devices and try "
				    "again.\n"));
			else
				fnvlist_add_string(buffer_t, "action",
				    "The pool cannot be "
				    "imported. Attach the missing "
				    "devices and try "
				    "again.");
			break;
		default:
			if (!json || (!json->json && !json->ld_json))
				(void) printf(gettext(" action: "
					"The pool cannot be "
				    "imported due to damaged "
				    "devices or data.\n"));
			else
				fnvlist_add_string(buffer_t, "action",
				    "The pool cannot be "
				    "imported due to damaged "
				    "devices or data.");
		}
	}

	/* Print the comment attached to the pool. */
	if (nvlist_lookup_string(config, ZPOOL_CONFIG_COMMENT, &comment) == 0) {
		if (!json || (!json->json && !json->ld_json))
			(void) printf(gettext("comment: %s\n"), comment);
		else
			fnvlist_add_string(buffer_t, "comment", comment);
	}

	/*
	 * If the state is "closed" or "can't open", and the aux state
	 * is "corrupt data":
	 */
	if (((vs->vs_state == VDEV_STATE_CLOSED) ||
	    (vs->vs_state == VDEV_STATE_CANT_OPEN)) &&
	    (vs->vs_aux == VDEV_AUX_CORRUPT_DATA)) {
		if (pool_state == POOL_STATE_DESTROYED) {
			if (!json || (!json->json && !json->ld_json))
				(void) printf(gettext("\tThe pool was "
					"destroyed, but can be imported using "
				    "the '-Df' flags.\n"));
			else
				fnvlist_add_string(json->nv_dict_error,
				    "error", "The pool was destroyed, "
				    "but can be imported using "
				    "the '-Df' flags.");
		} else if (pool_state != POOL_STATE_EXPORTED) {
			if (!json || (!json->json && !json->ld_json))
				(void) printf(gettext("\tThe pool may be "
					"active on another system, but can be "
					"imported using\n\tthe '-f' flag.\n"));
			else
				fnvlist_add_string(json->nv_dict_error, "error",
				    "The pool may be active on "
				    "another system, but can be imported using "
				    "the '-f' flag.");
		}
	}

	if (msgid != NULL) {
		if (!json || (!json->json && !json->ld_json))
			(void) printf(gettext("   see: "
			    "http://zfsonlinux.org/msg/%s\n"),
			    msgid);
		else {
			(void) snprintf(buffer, sizeof (buffer),
			    gettext("http://zfsonlinux.org/msg/%s"),
			    msgid);
			fnvlist_add_string(buffer_t, "see", buffer);
		}
	}

	if (!json || (!json->json && !json->ld_json))
		(void) printf(gettext(" config:\n\n"));

	namewidth = max_width(NULL, nvroot, 0, 0, 0);
	if (namewidth < 10)
		namewidth = 10;

	print_import_config(name, nvroot, namewidth, 0, 0, json, buffer_t);
	if (num_logs(nvroot) > 0)
		print_logs(NULL, nvroot, namewidth, B_FALSE, 0,
		    json, config_buffer);
	if (json && (json->json || json->ld_json)) {
		fnvlist_add_nvlist(buffer_t, "config", config_buffer);
		fnvlist_free(config_buffer);
	}

	if (reason == ZPOOL_STATUS_BAD_GUID_SUM) {
		if (!json || (!json->json && !json->ld_json))
			(void) printf(gettext("\n\tAdditional devices "
				"are known to "
			    "be part of this pool, though their\n\texact "
			    "configuration cannot be determined.\n"));
		else
			fnvlist_add_string(json->nv_dict_error, "error",
			    "Additional devices are known to "
			    "be part of this pool, though their exact "
			    "configuration cannot be determined.");
	}

	if (json && (json->json || json->ld_json)) {
		json->json_data = realloc(json->json_data,
		    sizeof (nvlist_t *) * json->nb_elem);
		((nvlist_t **)json->json_data)[json->nb_elem - 1] =
		    buffer_t;
		if (json->ld_json) {
			nvlist_print_json(stdout, buffer_t);
			fprintf(stdout, "\n");
			fflush(stdout);
		}
	}
}

/*
 * Perform the import for the given configuration.  This passes the heavy
 * lifting off to zpool_import_props(), and then mounts the datasets contained
 * within the pool.
 */
static int
do_import(nvlist_t *config, const char *newname, const char *mntopts,
    nvlist_t *props, int flags, zfs_json_t *json)
{
	zpool_handle_t *zhp;
	char *name;
	uint64_t state;
	uint64_t version;
	char errbuf[1024];

	verify(nvlist_lookup_string(config, ZPOOL_CONFIG_POOL_NAME,
	    &name) == 0);

	verify(nvlist_lookup_uint64(config,
	    ZPOOL_CONFIG_POOL_STATE, &state) == 0);
	verify(nvlist_lookup_uint64(config,
	    ZPOOL_CONFIG_VERSION, &version) == 0);
	if (!SPA_VERSION_IS_SUPPORTED(version)) {
		(void) snprintf(errbuf, sizeof (errbuf),
		    gettext("cannot import '%s': pool "
		    "is formatted using an unsupported ZFS version\n"), name);
		if (!json || (!json->json && !json->ld_json))
			fprintf(stderr, "%s\n", errbuf);
		else
			fnvlist_add_string(json->nv_dict_error,
			    "error", errbuf);
		return (1);
	} else if (state != POOL_STATE_EXPORTED &&
	    !(flags & ZFS_IMPORT_ANY_HOST)) {
		uint64_t hostid = 0;
		unsigned long system_hostid = get_system_hostid();

		(void) nvlist_lookup_uint64(config, ZPOOL_CONFIG_HOSTID,
		    &hostid);

		if (hostid != 0 && (unsigned long)hostid != system_hostid) {
			char *hostname;
			uint64_t timestamp;
			time_t t;

			verify(nvlist_lookup_string(config,
			    ZPOOL_CONFIG_HOSTNAME, &hostname) == 0);
			verify(nvlist_lookup_uint64(config,
			    ZPOOL_CONFIG_TIMESTAMP, &timestamp) == 0);
			t = timestamp;
			(void) snprintf(errbuf, sizeof (errbuf),
			    gettext("cannot import "
			    "'%s': pool may be in use from other "
			    "system, it was last accessed by %s "
			    "(hostid: 0x%lx) on %s"
			    " use '-f' to "
			    "import anyway"),
			    name, hostname,
			    (unsigned long)hostid,
			    asctime(localtime(&t)));

			if (!json || (!json->json && !json->ld_json))
				fprintf(stderr, "%s\n", errbuf);
			else
				fnvlist_add_string(json->nv_dict_error,
			    "error", errbuf);
			return (1);
		}
	}

	if (zpool_import_props(g_zfs, config, newname, props, flags, json) != 0)
		return (1);

	if (newname != NULL)
		name = (char *)newname;

	if ((zhp = zpool_open_canfail(json, g_zfs, name)) == NULL)
		return (1);

	if (zpool_get_state(zhp) != POOL_STATE_UNAVAIL &&
	    !(flags & ZFS_IMPORT_ONLY) &&
	    zpool_enable_datasets(zhp, mntopts, 0, NULL) != 0) {
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
	zfs_json_t json;
	json.json = json.ld_json = B_FALSE;
	char errbuf[1024];

	/* check options */
	while ((c = getopt(argc, argv, ":aCc:d:DEfFmnNo:R:stT:VXjJ")) != -1) {
		switch (c) {
		case 'j':
			if (json.json || json.ld_json)
				break;
			json.ld_json = B_TRUE;
			json.nv_dict_props = fnvlist_alloc();
			json.nv_dict_error = fnvlist_alloc();
			fnvlist_add_string(json.nv_dict_error, "error", "");
			json.nb_elem = 0;
			json.json_data = NULL;
			break;
		case 'J':
			if (json.json)
				break;
			json.json = B_TRUE;
			if (!json.ld_json) {
				json.nv_dict_props = fnvlist_alloc();
				json.nv_dict_error = fnvlist_alloc();
			} else
				json.ld_json = B_FALSE;
			json.nb_elem = 0;
			json.json_data = NULL;
			fnvlist_add_string(json.nv_dict_props,
			    "cmd", "zpool import");
			fnvlist_add_string(json.nv_dict_error, "error", "");
			break;
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
				    &props, B_TRUE, NULL))
					goto error;
			} else {
				mntopts = optarg;
			}
			break;
		case 'R':
			if (add_prop_list(zpool_prop_to_name(
			    ZPOOL_PROP_ALTROOT), optarg, &props, B_TRUE, NULL))
				goto error;
			if (add_prop_list_default(zpool_prop_to_name(
			    ZPOOL_PROP_CACHEFILE),
			    "none", &props, B_TRUE, NULL))
				goto error;
			break;
		case 's':
			do_scan = B_TRUE;
			break;
		case 't':
			flags |= ZFS_IMPORT_TEMP_NAME;
			if (add_prop_list_default(zpool_prop_to_name(
			    ZPOOL_PROP_CACHEFILE),
			    "none", &props, B_TRUE, NULL))
				goto error;
			break;

		case 'T':
			errno = 0;
			txg = strtoull(optarg, &endptr, 0);
			if (errno != 0 || *endptr != '\0') {
				(void) snprintf(errbuf,
				    sizeof (errbuf),
				    gettext("invalid txg value\n"));
				if (!json.json && !json.ld_json)
					fprintf(stderr, "%s\n", errbuf);
				else
					fnvlist_add_string(json.nv_dict_error,
					    "error", errbuf);
				goto json_usage;
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
			(void) snprintf(errbuf, sizeof (errbuf),
			    gettext("missing argument for "
			    "'%c' option"), optopt);
			if (!json.json && !json.ld_json)
				fprintf(stderr, "%s\n", errbuf);
			else
				fnvlist_add_string(json.nv_dict_error,
				    "error", errbuf);
			goto json_usage;
			break;
		case '?':
			(void) snprintf(errbuf, sizeof (errbuf),
			    gettext("invalid option '%c'"),
			    optopt);
			if (!json.json && !json.ld_json)
				fprintf(stderr, "%s\n", errbuf);
			else
				fnvlist_add_string(json.nv_dict_error,
				    "error", errbuf);
			goto json_usage;
		}
	}

	argc -= optind;
	argv += optind;

	if (cachefile && nsearch != 0) {
		(void) snprintf(errbuf, sizeof (errbuf),
		    gettext("-c is incompatible with -d"));
		if (!json.json && !json.ld_json)
			fprintf(stderr, "%s\n", errbuf);
		else
			fnvlist_add_string(json.nv_dict_error,
			    "error", errbuf);
		goto json_usage;
	}

	if ((dryrun || xtreme_rewind) && !do_rewind) {
		(void) snprintf(errbuf, sizeof (errbuf),
		    gettext("-n or -X only meaningful with -F"));
		if (!json.json && !json.ld_json)
			fprintf(stderr, "%s\n", errbuf);
		else
			fnvlist_add_string(json.nv_dict_error,
			    "error", errbuf);
		goto json_usage;
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
			(void) snprintf(errbuf, sizeof (errbuf),
			    gettext("too many arguments"));
			if (!json.json && !json.ld_json)
				fprintf(stderr, "%s\n", errbuf);
			else
				fnvlist_add_string(json.nv_dict_error,
				    "error", errbuf);
			goto json_usage;
		}
	} else {
		if (argc > 2) {
			(void) snprintf(errbuf, sizeof (errbuf),
			    gettext("too many arguments"));
			if (!json.json && !json.ld_json)
				fprintf(stderr, "%s\n", errbuf);
			else
				fnvlist_add_string(json.nv_dict_error,
				    "error", errbuf);
			goto json_usage;
		}

		/*
		 * Check for the SYS_CONFIG privilege.  We do this explicitly
		 * here because otherwise any attempt to discover pools will
		 * silently fail.
		 */
		if (argc == 0 && !priv_ineffect(PRIV_SYS_CONFIG)) {
			(void) snprintf(errbuf, sizeof (errbuf),
			    gettext("cannot "
			    "discover pools: permission denied"));
			if (!json.json && !json.ld_json)
				fprintf(stderr, "%s\n", errbuf);
			else
				fnvlist_add_string(json.nv_dict_error,
				    "error", errbuf);
			if (searchdirs != NULL)
				free(searchdirs);

			nvlist_free(policy);
			err = 1;
			goto error;
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
	pools = zpool_search_import(g_zfs, &idata, &json);
	thread_fini();

	if (pools != NULL && idata.exists &&
	    (argc == 1 || strcmp(argv[0], argv[1]) == 0)) {
		if (!json.json && !json.ld_json) {
			(void) fprintf(stderr, gettext("cannot import '%s': "
			    "a pool with that name already exists\n"),
			    argv[0]);
			(void) fprintf(stderr, gettext("use the form '%s "
			    "<pool | id> <newpool>' to give it a new name\n"),
			    "zpool import");
		} else {
			(void) snprintf(errbuf, sizeof (errbuf),
			    gettext("cannot import '%s': "
			    "a pool with that name already exists"
			    " use the form '%s "
			    "<pool | id> <newpool>' to give it a new name"),
			    argv[0], "zpool import");
			fnvlist_add_string(json.nv_dict_error,
			    "error", errbuf);
		}
		err = 1;
	} else if (pools == NULL && idata.exists) {
		if (!json.json && !json.ld_json) {
			(void) fprintf(stderr, gettext("cannot import '%s': "
			    "a pool with that name is "
			    "already created/imported,\n"),
			    argv[0]);
			(void) fprintf(stderr,
			    gettext("and no additional pools "
			    "with that name were found\n"));
		} else {
			(void) snprintf(errbuf, sizeof (errbuf),
			    gettext("cannot import '%s': "
			    "a pool with that name is "
			    "already created/imported,"
			    " and no additional pools "
			    "with that name were found"), argv[0]);
			fnvlist_add_string(json.nv_dict_error,
			    "error", errbuf);
		}
		err = 1;
	} else if (pools == NULL) {
		if (argc != 0) {
			(void) snprintf(errbuf, sizeof (errbuf),
			    gettext("cannot import '%s': "
			    "no such pool available"), argv[0]);
			if (!json.json && !json.ld_json)
				fprintf(stderr, "%s\n", errbuf);
			else
				fnvlist_add_string(json.nv_dict_error,
				    "error", errbuf);
		}
		err = 1;
	}

	if (err == 1) {
		goto error;
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
				if (!json.json && !json.ld_json)
					(void) printf("\n");

			if (do_all) {
				err |= do_import(config, NULL, mntopts,
				    props, flags, &json);
			} else {
				show_import(config, &json);
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
					if (!json.json && !json.ld_json) {
						(void) fprintf(stderr, gettext(
						    "cannot import '%s':"
						    " more than "
						    "one matching pool\n"),
						    searchname);
						(void) fprintf(stderr, gettext(
						    "import by numeric ID "
						    "instead\n"));
					} else {
						(void) snprintf(errbuf,
						    sizeof (errbuf), gettext(
						    "cannot import '%s': more "
						    "than one matching pool"
						    " import by "
						    "numeric ID instead"),
						    searchname);
						fnvlist_add_string(
						    json.nv_dict_error,
						    "error", errbuf);
					}
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
			(void) snprintf(errbuf, sizeof (errbuf),
			    gettext("cannot import '%s': "
			    "no such pool available"), argv[0]);
			if (!json.json && !json.ld_json)
				fprintf(stderr, "%s\n", errbuf);
			else
				fnvlist_add_string(json.nv_dict_error,
				    "error", errbuf);
			err = B_TRUE;
		} else {
			err |= do_import(found_config, argc == 1 ? NULL :
			    argv[1], mntopts, props, flags, &json);
		}
	}

	/*
	 * If we were just looking for pools, report an error if none were
	 * found.
	 */
	if (argc == 0 && first) {
		(void) snprintf(errbuf, sizeof (errbuf),
		    gettext("no pools available to import"));
		if (!json.json && !json.ld_json)
			fprintf(stderr, "%s\n", errbuf);
		else
			fnvlist_add_string(json.nv_dict_error,
			    "error", errbuf);
	}

error:
	nvlist_free(props);
	nvlist_free(pools);
	nvlist_free(policy);
	if (searchdirs != NULL)
		free(searchdirs);
	if (envdup != NULL)
		free(envdup);

	if (json.json || json.ld_json) {
		if (json.json) {
			fnvlist_add_string(json.nv_dict_props,
			    "schema_version", "1.0");
			fnvlist_add_nvlist_array(json.nv_dict_props, "stdout",
			    (nvlist_t **)json.json_data, json.nb_elem);
			fnvlist_add_nvlist(json.nv_dict_props,
			    "stderr", json.nv_dict_error);
			nvlist_print_json(stdout, json.nv_dict_props);
		} else
			nvlist_print_json(stdout, json.nv_dict_error);
		fprintf(stdout, "\n");
		fflush(stdout);
		while (((json.nb_elem)--) > 0)
			fnvlist_free(
			    ((nvlist_t **)
			    (json.json_data))[json.nb_elem]);
		free(json.json_data);
		fnvlist_free(json.nv_dict_error);
		fnvlist_free(json.nv_dict_props);
	}

	return (err ? 1 : 0);
	json_usage:
	nvlist_free(props);
	nvlist_free(pools);
	nvlist_free(policy);
	if (searchdirs != NULL)
		free(searchdirs);
	if (envdup != NULL)
		free(envdup);
	if (!json.json && !json.ld_json)
		usage(B_FALSE);
	else {
		if (json.json) {
			fnvlist_add_string(json.nv_dict_props,
			    "schema_version", "1.0");
			fnvlist_add_nvlist_array(json.nv_dict_props, "stdout",
			    (nvlist_t **)json.json_data, json.nb_elem);
			fnvlist_add_nvlist(json.nv_dict_props,
			    "stderr", json.nv_dict_error);
			nvlist_print_json(stdout, json.nv_dict_props);
		} else
			nvlist_print_json(stdout, json.nv_dict_error);
		fprintf(stdout, "\n");
		fflush(stdout);
		while (((json.nb_elem)--) > 0)
			fnvlist_free(
			    ((nvlist_t **)
			    (json.json_data))[json.nb_elem]);
		free(json.json_data);
		fnvlist_free(json.nv_dict_error);
		fnvlist_free(json.nv_dict_props);
	}
	exit(2);
}

typedef struct iostat_cbdata {
	boolean_t cb_verbose;
	int cb_name_flags;
	int cb_namewidth;
	int cb_iteration;
	zpool_list_t *cb_list;
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

char *
get_one_stat(uint64_t value)
{
	char buf[64];

	zfs_nicenum(value, buf, sizeof (buf));
	return (strdup(buf));
}

char *
get_timestamp(uint_t timestamp_fmt)
{
	time_t t = time(NULL);
	static char *fmt = NULL;
	char buff[512];
	/* We only need to retrieve this once per invocation */
	if (fmt == NULL)
		fmt = nl_langinfo(_DATE_FMT);

	if (timestamp_fmt == UDATE) {
		(void) snprintf(buff, sizeof (buff), "%ld", t);
	} else if (timestamp_fmt == DDATE) {
		char dstr[64];
		int len;

		len = strftime(dstr, sizeof (dstr), fmt, localtime(&t));
		if (len > 0)
			(void) snprintf(buff, sizeof (buff), "%s", dstr);
	}
	return (strdup(buff));
}

/*
 * Print out all the statistics for the given vdev.  This can either be the
 * toplevel configuration, or called recursively.  If 'name' is NULL, then this
 * is a verbose output, and we don't want to display the toplevel pool stats.
 */

void
print_vdev_stats(zpool_handle_t *zhp, const char *name, nvlist_t *oldnv,
    nvlist_t *newnv, iostat_cbdata_t *cb, int depth,
    zfs_json_t *json, int child)
{
	nvlist_t **oldchild, **newchild;
	uint_t c, children;
	vdev_stat_t *oldvs, *newvs;
	vdev_stat_t zerovs = { 0 };
	uint64_t tdelta;
	double scale;
	char *vname;
	char *buf = NULL;
	nvlist_t	*buffer = NULL;
	nvlist_t	*buffer3 = NULL;
	nvlist_t	*buffer2 = NULL;
	nvlist_t	*stat = NULL;
	void		*buffer_t = NULL;
	void		* buffer_t2 = NULL;
	void		* buffer_t3 = NULL;
	int 		nb_elem_buff2 = 0;
	int 		nb_elem_buff3 = 0;
	int 		nb_elem_buff = 0;


	if (json->json || json->ld_json) {
		buffer = fnvlist_alloc();
		stat = fnvlist_alloc();
		buffer2 = fnvlist_alloc();
		buffer3 = fnvlist_alloc();
	}

	if (oldnv != NULL) {
		verify(nvlist_lookup_uint64_array(oldnv,
		    ZPOOL_CONFIG_VDEV_STATS, (uint64_t **)&oldvs, &c) == 0);
	} else {
		oldvs = &zerovs;
	}

	verify(nvlist_lookup_uint64_array(newnv, ZPOOL_CONFIG_VDEV_STATS,
	    (uint64_t **)&newvs, &c) == 0);
	if (!json->json && !json->ld_json) {
			if (strlen(name) + depth > cb->cb_namewidth)
				(void) printf("%*s%s", depth, "", name);
			else
				(void) printf("%*s%s%*s", depth, "", name,
				    (int)(cb->cb_namewidth -
				    strlen(name) - depth), "");

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
				print_one_stat(newvs->vs_space -
				    newvs->vs_alloc);
			}
			print_one_stat((uint64_t)(scale *
			    (newvs->vs_ops[ZIO_TYPE_READ] -
			    oldvs->vs_ops[ZIO_TYPE_READ])));

			print_one_stat((uint64_t)(scale *
			    (newvs->vs_ops[ZIO_TYPE_WRITE] -
			    oldvs->vs_ops[ZIO_TYPE_WRITE])));

			print_one_stat((uint64_t)(scale *
			    (newvs->vs_bytes[ZIO_TYPE_READ] -
			    oldvs->vs_bytes[ZIO_TYPE_READ])));

			print_one_stat((uint64_t)(scale *
			    (newvs->vs_bytes[ZIO_TYPE_WRITE] -
			    oldvs->vs_bytes[ZIO_TYPE_WRITE])));

			(void) printf("\n");

			if (!cb->cb_verbose)
				return;

			if (nvlist_lookup_nvlist_array(newnv,
			    ZPOOL_CONFIG_CHILDREN, &newchild, &children) != 0)
				return;

			if (oldnv && nvlist_lookup_nvlist_array(oldnv,
			    ZPOOL_CONFIG_CHILDREN, &oldchild, &c) != 0)
				return;

			for (c = 0; c < children; c++) {
				uint64_t ishole = B_FALSE, islog = B_FALSE;

				(void) nvlist_lookup_uint64(newchild[c],
				    ZPOOL_CONFIG_IS_HOLE, &ishole);

				(void) nvlist_lookup_uint64(newchild[c],
				    ZPOOL_CONFIG_IS_LOG, &islog);

				if (ishole || islog)
					continue;

				vname = zpool_vdev_name(g_zfs, zhp,
				    newchild[c], cb->cb_name_flags);
				print_vdev_stats(zhp, vname,
				    oldnv ? oldchild[c] : NULL,
				    newchild[c], cb, depth + 2, json, 0);
				free(vname);
			}

			/*
			 * Log device section
			 */

			if (num_logs(newnv) > 0) {
				(void) printf("%-*s      -      -   "
				    "   -      -      -      "
				    "-\n", cb->cb_namewidth, "logs");

				for (c = 0; c < children; c++) {
					uint64_t islog = B_FALSE;
					(void) nvlist_lookup_uint64(newchild[c],
					    ZPOOL_CONFIG_IS_LOG, &islog);

					if (islog) {
						vname = zpool_vdev_name(g_zfs,
						    zhp, newchild[c], B_FALSE);
						print_vdev_stats(zhp,
						    vname, oldnv ?
						    oldchild[c] : NULL,
						    newchild[c],
						    cb, depth + 2, json, 0);
						free(vname);
					}
				}
			}

			/*
			 * Include level 2 ARC devices in iostat output
			 */
			if (nvlist_lookup_nvlist_array(newnv,
			    ZPOOL_CONFIG_L2CACHE, &newchild,
			    &children) != 0)
				return;

			if (oldnv && nvlist_lookup_nvlist_array(oldnv,
			    ZPOOL_CONFIG_L2CACHE, &oldchild, &c) != 0)
				return;

			if (children > 0) {
				(void) printf("%-*s      -      -"
					"      -      -      -      "
				    "-\n", cb->cb_namewidth, "cache");
				for (c = 0; c < children; c++) {
					vname = zpool_vdev_name(g_zfs,
					    zhp, newchild[c], B_FALSE);
					print_vdev_stats(zhp, vname,
					    oldnv ? oldchild[c] : NULL,
					    newchild[c], cb, depth + 2,
					    json, 1);
					free(vname);
				}
			}
		} else {
			fnvlist_add_string(stat, "name", name);
			tdelta = newvs->vs_timestamp - oldvs->vs_timestamp;
			if (json->ld_json && json->timestamp) {
				fnvlist_add_string(stat, "date",
			    fnvlist_lookup_string(json->nv_dict_props,
				    "date"));
			}
			if (tdelta == 0)
				scale = 1.0;
			else
				scale = (double)NANOSEC / tdelta;

		/* only toplevel vdevs have capacity stats */
			if (newvs->vs_space == 0) {
				fnvlist_add_string(buffer, "alloc",
				    "-");
				fnvlist_add_string(buffer, "free",
				    "-");
			} else {
				buf =  get_one_stat(newvs->vs_alloc);
				fnvlist_add_string(buffer, "alloc",
				    buf);
				free(buf);
				buf =  get_one_stat(newvs->vs_space -
				    newvs->vs_alloc);
				fnvlist_add_string(buffer, "free",
				    buf);
				free(buf);
			}
				nb_elem_buff ++;

				buffer_t = realloc(buffer_t,
				    sizeof (nvlist_t *) * nb_elem_buff);
				((nvlist_t **)buffer_t)[nb_elem_buff - 1] =
				    buffer;
				fnvlist_add_nvlist_array(stat, "capacity",
				    (nvlist_t **)buffer_t, nb_elem_buff);

				while ((nb_elem_buff--) > 0)
					fnvlist_free(((nvlist_t **)buffer_t)
					    [nb_elem_buff]);
				free(buffer_t);

				nb_elem_buff2++;

				buf =  get_one_stat((uint64_t)(scale *
				    (newvs->vs_ops[ZIO_TYPE_READ] -
				    oldvs->vs_ops[ZIO_TYPE_READ])));
				fnvlist_add_string(buffer2, "read",
				    buf);
				free(buf);
				buf = get_one_stat((uint64_t)
				    (scale * (newvs->vs_ops[ZIO_TYPE_WRITE] -
				    oldvs->vs_ops[ZIO_TYPE_WRITE])));
				fnvlist_add_string(buffer2, "write",
				    buf);
				free(buf);
				buffer_t2 = realloc(buffer_t2,
				    sizeof (nvlist_t *) * nb_elem_buff2);
				((nvlist_t **)buffer_t2)[nb_elem_buff2 - 1] =
				    buffer2;
				fnvlist_add_nvlist_array(stat, "operations",
				    (nvlist_t **)buffer_t2, nb_elem_buff2);

				while ((nb_elem_buff2--) > 0)
					fnvlist_free(((nvlist_t **)buffer_t2)
					    [nb_elem_buff2]);
				free(buffer_t2);

				nb_elem_buff3++;
				buf = get_one_stat((uint64_t)
				    (scale * (newvs->vs_bytes[ZIO_TYPE_READ] -
				    oldvs->vs_bytes[ZIO_TYPE_READ])));
				fnvlist_add_string(buffer3, "read",
				    buf);
				free(buf);
				buf = get_one_stat((uint64_t)
				    (scale * (newvs->vs_bytes[ZIO_TYPE_WRITE] -
				    oldvs->vs_bytes[ZIO_TYPE_WRITE])));
				fnvlist_add_string(buffer3, "write",
				    buf);
				free(buf);


				buffer_t3 = realloc(buffer_t3,
				    sizeof (nvlist_t *) * nb_elem_buff3);
				((nvlist_t **)buffer_t3)[nb_elem_buff3 - 1] =
				    buffer3;
				fnvlist_add_nvlist_array(stat, "bandwidth",
				    (nvlist_t **)buffer_t3, nb_elem_buff3);

				while ((nb_elem_buff3--) > 0)
					fnvlist_free(((nvlist_t **)buffer_t3)
					    [nb_elem_buff3]);
				free(buffer_t3);

			if (!cb->cb_verbose) {
				if (!json->ld_json) {
					json->nb_elem++;
					json->json_data =
					    realloc(json->json_data,
					    sizeof (nvlist_t *)
					    * json->nb_elem);
					((nvlist_t **)json->json_data)
					    [json->nb_elem - 1] = stat;
				} else {
					nvlist_print_json(stdout, stat);
					fnvlist_free(stat);
					printf("\n");
				}
				return;
			}

			if (nvlist_lookup_nvlist_array(newnv,
			    ZPOOL_CONFIG_CHILDREN,
			    &newchild, &children) != 0) {
				if (child == 1) {
					json->nb_buff++;
					json->json_buffer =
					    realloc(json->json_buffer,
					    sizeof (nvlist_t *)
					    * json->nb_buff);
					((nvlist_t **)json->json_buffer)
					    [json->nb_buff - 1] = stat;
				} else if (child == 2) {
					json->nb_buff_cpy++;
					json->json_buffer_cpy =
					    realloc(json->json_buffer_cpy,
					    sizeof (nvlist_t *)
					    * json->nb_buff_cpy);
					((nvlist_t **)json->json_buffer_cpy)
					    [json->nb_buff_cpy - 1] = stat;
				}
				return;
			}

			if (oldnv && nvlist_lookup_nvlist_array(oldnv,
			    ZPOOL_CONFIG_CHILDREN,
			    &oldchild, &c) != 0) {
				json->nb_elem++;
				json->json_data = realloc(json->json_data,
				    sizeof (nvlist_t *) * json->nb_elem);
				((nvlist_t **)json->json_data)
				    [json->nb_elem - 1] = stat;
				return;
			}

			for (c = 0; c < children; c++) {
				uint64_t ishole = B_FALSE, islog = B_FALSE;

				(void) nvlist_lookup_uint64(newchild[c],
				    ZPOOL_CONFIG_IS_HOLE, &ishole);

				(void) nvlist_lookup_uint64(newchild[c],
				    ZPOOL_CONFIG_IS_LOG, &islog);

				if (ishole || islog)
					continue;

				vname = zpool_vdev_name(g_zfs, zhp,
				    newchild[c], B_FALSE);
				if (child == 1) {
					print_vdev_stats(zhp, vname,
					    oldnv ? oldchild[c] : NULL,
					    newchild[c], cb,
					    depth + 2, json, 2);
				} else {
					print_vdev_stats(zhp, vname,
					    oldnv ? oldchild[c] : NULL,
					    newchild[c], cb,
					    depth + 2, json, 1);
				}
				free(vname);
			}
			if (child == 0) {
				fnvlist_add_nvlist_array(stat, "devices",
				    (nvlist_t **)json->json_buffer,
				    json->nb_buff);
				while (((json->nb_buff)--) > 0)
					fnvlist_free(
					    ((nvlist_t **)(json->json_buffer))
					    [json->nb_buff]);
				free(json->json_buffer);
				json->json_buffer = NULL;
				json->nb_buff = 0;
			} else if (child == 1) {
				fnvlist_add_nvlist_array(stat, "devices",
				    (nvlist_t **)json->json_buffer_cpy,
				    json->nb_buff_cpy);
				while (((json->nb_buff_cpy)--) > 0)
					fnvlist_free(
					    ((nvlist_t **)
					    (json->json_buffer_cpy))
					    [json->nb_buff_cpy]);
				free(json->json_buffer_cpy);
				json->json_buffer_cpy = NULL;
				json->nb_buff_cpy = 0;
				json->nb_buff++;
				json->json_buffer = realloc(json->json_buffer,
				    sizeof (nvlist_t *) * json->nb_buff);
					((nvlist_t **)json->json_buffer)
					    [json->nb_buff - 1] = stat;
			}

			/*
			 * Log device section
			 */

			if (num_logs(newnv) > 0) {
				(void) printf("%-*s      -      -"
					"      -      -      -      "
				    "-\n", cb->cb_namewidth, "logs");

				for (c = 0; c < children; c++) {
					uint64_t islog = B_FALSE;
					(void) nvlist_lookup_uint64(newchild[c],
					    ZPOOL_CONFIG_IS_LOG, &islog);

					if (islog) {
						vname = zpool_vdev_name(g_zfs,
						    zhp, newchild[c], B_FALSE);
						print_vdev_stats(zhp,
						    vname, oldnv ?
						    oldchild[c] : NULL,
						    newchild[c], cb, depth + 2,
						    json, 0);
						free(vname);
					}
				}

			}

			/*
			 * Include level 2 ARC devices in iostat output
			 */
			if (nvlist_lookup_nvlist_array(newnv,
			    ZPOOL_CONFIG_L2CACHE, &newchild,
			    &children) != 0) {
				if (child == 0) {
				if (!json->ld_json) {
				json->nb_elem++;
				json->json_data = realloc(json->json_data,
				    sizeof (nvlist_t *) * json->nb_elem);
				((nvlist_t **)json->json_data)
				    [json->nb_elem - 1] = stat;
				} else {
					nvlist_print_json(stdout, stat);
					fnvlist_free(stat);
					printf("\n");
				}
				return;
				}
			}

			if (oldnv && nvlist_lookup_nvlist_array(oldnv,
			    ZPOOL_CONFIG_L2CACHE, &oldchild, &c) != 0) {
				return;
				}
			}
}

static int
refresh_iostat(zpool_handle_t *zhp, void *data, zfs_json_t *json)
{
	iostat_cbdata_t *cb = data;
	boolean_t missing;

	/*
	 * If the pool has disappeared, remove it from the list and continue.
	 */
	if (zpool_refresh_stats(zhp, &missing) != 0)
		return (-1);

	if (missing)
		pool_list_remove(cb->cb_list, zhp, json);

	return (0);
}

/*
 * Callback to print out the iostats for the given pool.
 */
int
print_iostat(zpool_handle_t *zhp, void *data, zfs_json_t *json)
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
	print_vdev_stats(zhp, zpool_get_name(zhp),
	    oldnvroot, newnvroot, cb,
	    0, json, 0);

	if (cb->cb_verbose && !json->json && !json->ld_json)
		print_iostat_separator(cb);

	return (0);
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
get_namewidth(zpool_handle_t *zhp, void *data, zfs_json_t *json)
{
	iostat_cbdata_t *cb = data;
	nvlist_t *config, *nvroot;
	int columns;

	if ((config = zpool_get_config(zhp, NULL)) != NULL) {
		verify(nvlist_lookup_nvlist(config, ZPOOL_CONFIG_VDEV_TREE,
		    &nvroot) == 0);
		if (!cb->cb_verbose)
			cb->cb_namewidth = strlen(zpool_get_name(zhp));
		else
			cb->cb_namewidth = max_width(zhp, nvroot, 0,
			    cb->cb_namewidth, cb->cb_name_flags);
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
get_interval_count(int *argcp, char **argv, unsigned long *iv,
    unsigned long *cnt)
{
	unsigned long interval = 0, count = 0;
	int argc = *argcp;

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
 * zpool iostat [-gLPv] [-T d|u] [pool] ... [interval [count]]
 *
 *	-g	Display guid for individual vdev name.
 *	-L	Follow links when resolving vdev path name.
 *	-P	Display full path for vdev name.
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
	boolean_t omit_since_boot = B_FALSE;
	boolean_t guid = B_FALSE;
	boolean_t follow_links = B_FALSE;
	boolean_t full_name = B_FALSE;
	iostat_cbdata_t cb = { 0 };
	zfs_json_t json;
	json.json = json.ld_json = json.timestamp = B_FALSE;
	json.json_buffer = NULL;
	json.nb_buff = 0;
	char errbuf[1024];
	int nb_loop = 0;
	char *buff;

	/* check options */
	while ((c = getopt(argc, argv, "JjgLPT:vy")) != -1) {
		switch (c) {
		case 'g':
			guid = B_TRUE;
			break;
		case 'L':
			follow_links = B_TRUE;
			break;
		case 'P':
			full_name = B_TRUE;
		case 'j':
			if (json.json || json.ld_json)
				break;
			json.nv_dict_props = fnvlist_alloc();
			json.nv_dict_error = fnvlist_alloc();
			json.nv_dict_buff = fnvlist_alloc();
			json.nv_dict_buff_cpy = fnvlist_alloc();
			json.ld_json = B_TRUE;
			json.json_buffer_cpy = NULL;
			json.nb_buff_cpy = 0;
			fnvlist_add_string(json.nv_dict_error,
			    "error", "");
			break;
		case 'J':
			if (json.json)
				break;
			if (!json.ld_json) {
				json.nv_dict_props = fnvlist_alloc();
				json.nv_dict_error = fnvlist_alloc();
				json.nv_dict_buff = fnvlist_alloc();
				json.nv_dict_buff_cpy = fnvlist_alloc();
			} else
				json.ld_json = B_FALSE;
			json.json = B_TRUE;
			json.json_data = NULL;
			json.json_buffer_cpy = NULL;
			json.nb_buff_cpy = 0;
			json.nb_elem = 0;
			fnvlist_add_string(json.nv_dict_props,
			    "schema_version", "1.0");
			fnvlist_add_string(json.nv_dict_error,
			    "error", "");
			break;
		case 'T':
			if (json.json || json.ld_json)
				json.timestamp = B_TRUE;
			get_timestamp_arg(*optarg);
			break;
		case 'v':
			verbose = B_TRUE;
			break;
		case 'y':
			omit_since_boot = B_TRUE;
			break;
		case '?':
			(void) snprintf(errbuf, sizeof (errbuf),
			    gettext("invalid option '%c'"),
			    optopt);
			if (!json.json && !json.ld_json)
				fprintf(stderr, "%s\n", errbuf);
			else
				fnvlist_add_string(json.nv_dict_error,
				    "error", errbuf);
			goto usage;
		}
	}

	argc -= optind;
	argv += optind;

	get_interval_count(&argc, argv, &interval, &count);

	/*
	 * Construct the list of all interesting pools.
	 */
	ret = 0;
	if ((list = pool_list_get(argc, argv, NULL, &ret, &json)) == NULL) {
		ret = 1;
		goto out;
	}

	if (pool_list_count(list, &json) == 0 && argc != 0) {
		pool_list_free(list, &json);
		ret = 1;
		goto out;
	}

	if (pool_list_count(list, &json) == 0 && interval == 0) {
		pool_list_free(list, &json);
		(void) snprintf(errbuf, sizeof (errbuf),
		    gettext("no pools available"));
		if (!json.json && ! json.ld_json)
			fprintf(stderr, "%s\n", errbuf);
		else
			fnvlist_add_string(json.nv_dict_error,
			    "error", errbuf);
		ret = 1;
		goto out;
	}
	/*
	 * Enter the main iostat loop.
	 */
	cb.cb_list = list;
	cb.cb_verbose = verbose;
	if (guid)
		cb.cb_name_flags |= VDEV_NAME_GUID;
	if (follow_links)
		cb.cb_name_flags |= VDEV_NAME_FOLLOW_LINKS;
	if (full_name)
		cb.cb_name_flags |= VDEV_NAME_PATH;
	cb.cb_iteration = 0;
	cb.cb_namewidth = 0;

	for (;;) {
		if ((npools = pool_list_count(list, &json)) == 0) {
			(void) snprintf(errbuf, sizeof (errbuf),
			    gettext("no pools available"));
			if (!json.json && !json.ld_json)
				fprintf(stderr, "%s\n", errbuf);
			else
				fnvlist_add_string(json.nv_dict_error,
				    "error", errbuf);
		} else {

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
				&cb, &json);

			/*
			 * Iterate over all pools to determine the maximum width
			 * for the pool / device name column across all pools.
			 */
			cb.cb_namewidth = 0;
			(void) pool_list_iter(list, B_FALSE, get_namewidth,
				&cb, &json);

			if (timestamp_fmt != NODATE) {
				if (!json.json && !json.ld_json)
					print_timestamp(timestamp_fmt);
				else {
					buff = get_timestamp(timestamp_fmt);
					fnvlist_add_string(json.nv_dict_props,
					    "date", buff);
					free(buff);
				}
			}

			/*
			 * If it's the first time and we're not skipping it,
			 * or either skip or verbose mode, print the header.
			 */
			if ((++cb.cb_iteration == 1 && !skip) ||
				(skip != verbose)) {
				if (!json.ld_json && !json.json)
					print_iostat_header(&cb);
		}

			if (skip) {
				(void) sleep(interval);
				continue;
			}
			if (!json.json) {
			(void) pool_list_iter(list,
			    B_FALSE, print_iostat, &cb, &json);
			} else if (nb_loop != 0) {
				(void) pool_list_iter(list,
				    B_FALSE, print_iostat, &cb, &json);
			}
			/*
			 * If there's more than one pool, and we're not in
			 * verbose mode (which prints a separator for us),
			 * then print a separator.
			 */
			if (npools > 1 && !verbose &&
			    !json.json && !json.ld_json)
				print_iostat_separator(&cb);

			if (verbose &&
			    (!json.ld_json && !json.json && !json.ld_json))
				(void) printf("\n");
			if (json.ld_json)
				printf("\n");
		}

		/*
		 * Flush the output so that redirection to a file isn't buffered
		 * indefinitely.
		 */
		(void) fflush(stdout);
		if (json.json) {
			nb_loop ++;
			if (nb_loop > 1) {
				interval = 0;
				break;
			}
		}

		if (interval == 0 && !json.json)
			break;

		if (count != 0 && --count == 0 && !json.json)
			break;

		(void) sleep(interval);
	}

	pool_list_free(list, &json);
out :
	if (json.json || json.ld_json) {
		if (json.json) {
			fnvlist_add_nvlist_array(json.nv_dict_props, "stdout",
			    (nvlist_t **)json.json_data, json.nb_elem);
			fnvlist_add_nvlist(json.nv_dict_props,
			    "stderr", json.nv_dict_error);
			nvlist_print_json(stdout, json.nv_dict_props);
			while (((json.nb_elem)--) > 0)
				fnvlist_free(
				    ((nvlist_t **)
				    (json.json_data))[json.nb_elem]);
			free(json.json_data);
		} else
			nvlist_print_json(stdout, json.nv_dict_error);
		printf("\n");
		fflush(stdout);
		if (verbose) {
			while (((json.nb_buff)--) > 0)
				fnvlist_free(
				    ((nvlist_t **)
				    (json.json_buffer))[json.nb_buff]);
			free(json.json_buffer);
		}
		while (((json.nb_buff_cpy)--) > 0)
			fnvlist_free(
			    ((nvlist_t **)
			    (json.json_buffer_cpy))[json.nb_buff_cpy]);
		free(json.json_buffer_cpy);
		fnvlist_free(json.nv_dict_error);
		fnvlist_free(json.nv_dict_props);
		fnvlist_free(json.nv_dict_buff);
		fnvlist_free(json.nv_dict_buff_cpy);
	}
	return (ret);
usage:
		if (!json.json && !json.ld_json)
			usage(B_FALSE);
		else if (json.json || json.ld_json) {
			if (json.json) {
				fnvlist_add_nvlist_array(json.nv_dict_props,
				    "stdout",
				    (nvlist_t **)json.json_data, json.nb_elem);
				fnvlist_add_nvlist(json.nv_dict_props,
				    "stderr", json.nv_dict_error);
				nvlist_print_json(stdout, json.nv_dict_props);
				while (((json.nb_elem)--) > 0)
					fnvlist_free(
					    ((nvlist_t **)
					    (json.json_data))[json.nb_elem]);
				free(json.json_data);
			} else
				nvlist_print_json(stdout, json.nv_dict_error);
			printf("\n");
			fflush(stdout);
			if (verbose) {
				while (((json.nb_buff)--) > 0)
					fnvlist_free(
					    ((nvlist_t **)
					    (json.json_buffer))[json.nb_buff]);
				free(json.json_buffer);
			}
			while (((json.nb_buff_cpy)--) > 0)
				fnvlist_free(
				    ((nvlist_t **)
				    (json.json_buffer_cpy))[json.nb_buff_cpy]);
			free(json.json_buffer_cpy);
			fnvlist_free(json.nv_dict_error);
			fnvlist_free(json.nv_dict_props);
			fnvlist_free(json.nv_dict_buff);
			fnvlist_free(json.nv_dict_buff_cpy);
		}
		exit(2);
}


typedef struct list_cbdata {
	boolean_t	cb_verbose;
	int		cb_name_flags;
	int		cb_namewidth;
	boolean_t	cb_scripted;
	zprop_list_t	*cb_proplist;
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
print_pool(zpool_handle_t *zhp, list_cbdata_t *cb, zfs_json_t *json)
{
	zprop_list_t *pl = cb->cb_proplist;
	boolean_t first = B_TRUE;
	char property[ZPOOL_MAXPROPLEN];
	char *propstr;
	boolean_t right_justify;
	size_t width;
	nvlist_t *test = (json->json || json->ld_json)
	    ? fnvlist_alloc() : NULL;


	for (; pl != NULL; pl = pl->pl_next) {

		width = pl->pl_width;
		if (first && cb->cb_verbose) {
			/*
			 * Reset the width to accommodate the verbose listing
			 * of devices.
			 */
			width = cb->cb_namewidth;
		}
		if (!first && !json->json && !json->ld_json) {
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
			    sizeof (property), NULL) != 0)
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
		if (!json->json && !json->ld_json) {
			if (cb->cb_scripted || (pl->pl_next
			    == NULL && !right_justify))
				(void) printf("%s", propstr);
			else if (right_justify)
				(void) printf("%*s", (int)width, propstr);
			else
				(void) printf("%-*s", (int)width, propstr);
		} else {
			if (pl->pl_prop != ZPROP_INVAL)
				fnvlist_add_string(test,
				    zpool_prop_column_name(pl->pl_prop),
				    propstr);
			else {
				int i;
				char headerbuf[ZPOOL_MAXPROPLEN];

				for (i = 0; pl->pl_user_prop[i] != '\0'; i++)
					headerbuf[i] =
				    toupper(pl->pl_user_prop[i]);
				headerbuf[i] = '\0';
				fnvlist_add_string(test,
				    headerbuf, propstr);
			}
		}
	}
	if (!json->json && !json->ld_json)
		printf("\n");
	else {
		if (json->timestamp && json->ld_json) {
				char *buff;
				buff = get_timestamp(timestamp_fmt);
				fnvlist_add_string(
				    test, "date", buff);
				free(buff);
		}
		json->nb_elem++;
		json->json_data = realloc(json->json_data,
		    sizeof (nvlist_t *) * json->nb_elem);
			((nvlist_t **)json->json_data)
			    [json->nb_elem - 1] = test;
	}
}

static void
print_one_column(zpool_prop_t prop, uint64_t value, boolean_t scripted,
    boolean_t valid)
{
	char propval[64];
	boolean_t fixed;
	size_t width = zprop_width(prop, &fixed, ZFS_TYPE_POOL);

	switch (prop) {
	case ZPOOL_PROP_EXPANDSZ:
		if (value == 0)
			(void) strlcpy(propval, "-", sizeof (propval));
		else
			zfs_nicenum(value, propval, sizeof (propval));
		break;
	case ZPOOL_PROP_FRAGMENTATION:
		if (value == ZFS_FRAG_INVALID) {
			(void) strlcpy(propval, "-", sizeof (propval));
		} else {
			(void) snprintf(propval, sizeof (propval), "%llu%%",
			    (unsigned long long)value);
		}
		break;
	case ZPOOL_PROP_CAPACITY:
		(void) snprintf(propval, sizeof (propval), "%llu%%",
		    (unsigned long long)value);
		break;
	default:
		zfs_nicenum(value, propval, sizeof (propval));
	}

	if (!valid)
		(void) strlcpy(propval, "-", sizeof (propval));

	if (scripted)
		(void) printf("\t%s", propval);
	else
		(void) printf("  %*s", (int)width, propval);
}

static char *
json_line_convert(zpool_prop_t prop, uint64_t value, boolean_t scripted,
    boolean_t valid)
{
	char propval[64];

	switch (prop) {
		case ZPOOL_PROP_EXPANDSZ:
			if (value == 0)
				(void) strlcpy(propval, "-", sizeof (propval));
			else
				zfs_nicenum(value, propval, sizeof (propval));
			snprintf(propval, sizeof (propval),
			    " %llu", (unsigned long long) value);
			break;
		case ZPOOL_PROP_FRAGMENTATION:
			if (value == ZFS_FRAG_INVALID) {
				(void) strlcpy(propval, "-", sizeof (propval));
			} else {
				(void) snprintf(propval, sizeof (propval),
				    "%llu%%", (unsigned long long)value);
			}
			break;
		case ZPOOL_PROP_CAPACITY:
			(void) snprintf(propval, sizeof (propval), "%llu",
			    (unsigned long long)value);
			break;
		default:
			zfs_nicenum(value, propval, sizeof (propval));
		}
	if (!valid)
		(void) strlcpy(propval, "-", sizeof (propval));
		snprintf(propval, sizeof (propval),
		    " %llu", (unsigned long long) value);
	return (strdup(propval));
}

void
print_list_stats(zpool_handle_t *zhp, const char *name, nvlist_t *nv,
    list_cbdata_t *cb, int depth, zfs_json_t *json)
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
		if (!json->json && !json->ld_json) {
			if (scripted)
				(void) printf("\t%s", name);
			else if (strlen(name) + depth > cb->cb_namewidth)
				(void) printf("%*s%s", depth, "", name);
			else
				(void) printf("%*s%s%*s", depth, "", name,
				    (int)(cb->cb_namewidth -
				    strlen(name) - depth), "");
		/*
		 * Print the properties for the individual vdevs. Some
		 * properties are only applicable to toplevel vdevs. The
		 * 'toplevel' boolean value is passed to the print_one_column()
		 * to indicate that the value is valid.
		 */
			print_one_column(ZPOOL_PROP_SIZE,
			    vs->vs_space, scripted, toplevel);
			print_one_column(ZPOOL_PROP_ALLOCATED,
			    vs->vs_alloc, scripted, toplevel);
			print_one_column(ZPOOL_PROP_FREE,
			    vs->vs_space - vs->vs_alloc, scripted, toplevel);
			print_one_column(ZPOOL_PROP_EXPANDSZ,
			    vs->vs_esize, scripted, B_TRUE);
			print_one_column(ZPOOL_PROP_FRAGMENTATION,
			    vs->vs_fragmentation, scripted,
			    (vs->vs_fragmentation !=
			    ZFS_FRAG_INVALID && toplevel));
			cap = (vs->vs_space == 0) ? 0 :
			    (vs->vs_alloc * 100 / vs->vs_space);
			print_one_column(ZPOOL_PROP_CAPACITY, cap,
			    scripted, toplevel);
			(void) printf("\n");
		} else {

			boolean_t toplevel = (vs->vs_space != 0);
			uint64_t cap;
			char *buff;

			json->nv_dict_buff_cpy = fnvlist_alloc();

			fnvlist_add_string(json->nv_dict_buff_cpy,
			    "name", name);
			buff = json_line_convert(ZPOOL_PROP_SIZE,
			    vs->vs_space, scripted, toplevel);
			fnvlist_add_string(json->nv_dict_buff_cpy, "size",
			    buff);
			free(buff);

			buff = json_line_convert(ZPOOL_PROP_ALLOCATED,
			    vs->vs_alloc, scripted, toplevel);
			fnvlist_add_string(json->nv_dict_buff_cpy, "allocated",
			    buff);
			free(buff);

			buff = json_line_convert(ZPOOL_PROP_FREE,
			    vs->vs_space - vs->vs_alloc, scripted, toplevel);
			fnvlist_add_string(json->nv_dict_buff_cpy, "free",
			    buff);
			free(buff);

			buff = json_line_convert(ZPOOL_PROP_EXPANDSZ,
			    vs->vs_esize, scripted, B_TRUE);
			fnvlist_add_string(json->nv_dict_buff_cpy, "expandsize",
			    buff);
			free(buff);

			buff = json_line_convert(ZPOOL_PROP_FRAGMENTATION,
			    vs->vs_fragmentation, scripted,
			    (vs->vs_fragmentation !=
			    ZFS_FRAG_INVALID && toplevel));
			fnvlist_add_string(json->nv_dict_buff_cpy,
			    "fragmentation",
			    buff);
			free(buff);

			cap = (vs->vs_space == 0) ? 0 :
			    (vs->vs_alloc * 100 / vs->vs_space);
			buff = json_line_convert(ZPOOL_PROP_CAPACITY,
			    cap, scripted, toplevel);
			fnvlist_add_string(json->nv_dict_buff_cpy, "capacity",
			    buff);
			free(buff);
			json->nb_buff++;
			json->json_buffer = realloc(json->json_buffer,
			    sizeof (nvlist_t *) * json->nb_buff);
		    ((nvlist_t **)json->json_buffer)[json->nb_buff - 1] =
			    json->nv_dict_buff_cpy;
		}
	}

	if (nvlist_lookup_nvlist_array(nv, ZPOOL_CONFIG_CHILDREN,
	    &child, &children) != 0) {
		    return;
	    }

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
		print_list_stats(zhp, vname, child[c], cb, depth + 2, json);
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
			print_list_stats(zhp, vname, child[c],
			    cb, depth + 2, json);
			free(vname);
		}
	}

	if (nvlist_lookup_nvlist_array(nv, ZPOOL_CONFIG_L2CACHE,
	    &child, &children) == 0 && children > 0) {
		/* LINTED E_SEC_PRINTF_VAR_FMT */
		if (!json->json || !json->ld_json) {
			(void) printf(dashes, cb->cb_namewidth, "cache");
			for (c = 0; c < children; c++) {
				vname = zpool_vdev_name(g_zfs, zhp, child[c],
				    cb->cb_name_flags);
				print_list_stats(zhp, vname, child[c],
				    cb, depth + 2, json);
				free(vname);
			}
		}
	}

	if (nvlist_lookup_nvlist_array(nv, ZPOOL_CONFIG_SPARES, &child,
	    &children) == 0 && children > 0) {
		/* LINTED E_SEC_PRINTF_VAR_FMT */
		if (!json->json || !json->ld_json)
			(void) printf(dashes, cb->cb_namewidth, "spare");
		for (c = 0; c < children; c++) {
			vname = zpool_vdev_name(g_zfs, zhp, child[c],
			    cb->cb_name_flags);
			print_list_stats(zhp, vname, child[c],
			    cb, depth + 2, json);
			free(vname);
		}
	}
}


/*
 * Generic callback function to list a pool.
 */
int
list_callback(zpool_handle_t *zhp, void *data, zfs_json_t *json)
{
	list_cbdata_t *cbp = data;
	nvlist_t *config;
	nvlist_t *nvroot;
	json->json_buffer = NULL;
	json->nb_buff = 0;

	config = zpool_get_config(zhp, NULL);
	print_pool(zhp, cbp, json);
	if (!cbp->cb_verbose) {
		if (json->ld_json) {
			nvlist_print_json(stdout,
			    ((nvlist_t **)json->json_data)
			    [json->nb_elem -1]);
			printf("\n");
		}
		return (0);
	}
	verify(nvlist_lookup_nvlist(config, ZPOOL_CONFIG_VDEV_TREE,
	    &nvroot) == 0);
	if (json->json || json->ld_json) {
		json->json_buffer = NULL;
		json->nb_buff = 0;
	}
	print_list_stats(zhp, NULL, nvroot, cbp, 0, json);
	if (json->json || json->ld_json) {
		if (json->json) {
			fnvlist_add_nvlist_array(
			    ((nvlist_t **)json->json_data)
			    [json->nb_elem -1],
			    "Devices",
			    (nvlist_t **)json->json_buffer,
			    json->nb_buff);
		} if (json->ld_json) {
			fnvlist_add_nvlist_array(
			    ((nvlist_t **)json->json_data)
			    [json->nb_elem -1],
			    "Devices",
			    (nvlist_t **)json->json_buffer,
			    json->nb_buff);
			nvlist_print_json(stdout,
			    ((nvlist_t **)json->json_data)
			    [json->nb_elem -1]);
			printf("\n");
		}
		while (((json->nb_buff)--) > 0)
			fnvlist_free(
		    ((nvlist_t **)
		    (json->json_buffer))[json->nb_buff]);
		free(json->json_buffer);
	}
	return (0);
}

/*
 * zpool list [-gHLP] [-o prop[,prop]*] [-T d|u] [pool] ... [interval [count]]
 *
 *	-g	Display guid for individual vdev name.
 *	-H	Scripted mode.  Don't display headers, and separate properties
 *		by a single tab.
 *	-L	Follow links when resolving vdev path name.
 *	-o	List of properties to display.  Defaults to
 *		"name,size,allocated,free,expandsize,fragmentation,capacity,"
 *		"dedupratio,health,altroot"
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
	unsigned long interval = 0, count = 0;
	zpool_list_t *list = NULL;
	boolean_t first = B_TRUE;
	zfs_json_t json = {0};
	json.json = json.ld_json = B_FALSE;
	json.timestamp = B_FALSE;
	char errbuf[1024];

	/* check options */
	while ((c = getopt(argc, argv, ":JjgHLo:PT:v")) != -1) {
		switch (c) {
		case 'g':
			cb.cb_name_flags |= VDEV_NAME_GUID;
			break;
		case 'j':
			if (json.json || json.ld_json)
				break;
			json.nv_dict_error = fnvlist_alloc();
			json.nv_dict_props = fnvlist_alloc();
			json.json_data = NULL;
			json.nb_elem = 0;
			fnvlist_add_string(json.nv_dict_error, "error", "");
			json.ld_json = B_TRUE;
			break;
		case 'J':
			if (json.json)
				break;
			if (!json.ld_json) {
				json.nv_dict_error = fnvlist_alloc();
				json.nv_dict_props = fnvlist_alloc();
			} else
				json.ld_json = B_FALSE;
			json.json_data = NULL;
			json.json_data = NULL;
			json.json = B_TRUE;
			json.ld_json = B_FALSE;
			json.nb_elem = 0;
			json.nb_elem = 0;
			fnvlist_add_string(json.nv_dict_props,
			    "command", "zpool list");
			fnvlist_add_string(json.nv_dict_error, "error", "");
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
		case 'T':
			json.timestamp = B_TRUE;
			get_timestamp_arg(*optarg);
			break;
		case 'v':
			cb.cb_verbose = B_TRUE;
			break;
		case ':':
			(void) snprintf(errbuf,
			    sizeof (errbuf),
			    gettext("missing argument for "
			    "'%c' option"), optopt);
			if (!json.json && !json.ld_json)
				fprintf(stderr, "%s\n", errbuf);
			else
				fnvlist_add_string(json.nv_dict_error,
				    "error", errbuf);
			goto json_usage;
		case '?':
			(void) snprintf(errbuf, sizeof (errbuf),
			    gettext("invalid option '%c'"),
			    optopt);
			if (!json.json && !json.ld_json)
				fprintf(stderr, "%s\n", errbuf);
			else
				fnvlist_add_string(json.nv_dict_error,
				    "error", errbuf);
			goto json_usage;
		}
	}

	argc -= optind;
	argv += optind;

	get_interval_count(&argc, argv, &interval, &count);

	if (zprop_get_list(&json, g_zfs, props,
	    &cb.cb_proplist, ZFS_TYPE_POOL) != 0) {
	    if (json.json || json.ld_json)
			fnvlist_add_string(json.nv_dict_error,
			    "error", "bad usage");
		goto json_usage;
	}

	for (;;) {
		if ((list = pool_list_get(argc, argv, &cb.cb_proplist,
		    &ret, &json)) == NULL) {
			if (!json.json && !json.ld_json)
				return (1);
			else {
				ret = 1;
				goto json_out;
			}
		}
		if (pool_list_count(list, &json) == 0)
			break;

		if (timestamp_fmt != NODATE && !json.json && !json.ld_json)
			print_timestamp(timestamp_fmt);

		if (!cb.cb_scripted && (first ||
		    cb.cb_verbose) && !json.json && !json.ld_json) {
			print_header(&cb);
			first = B_FALSE;
		}
		ret = pool_list_iter(list, B_TRUE, list_callback, &cb, &json);

		if (interval == 0)
			break;

		if (count != 0 && --count == 0)
			break;

		pool_list_free(list, &json);
		(void) sleep(interval);
	}

	if (argc == 0 && !cb.cb_scripted && pool_list_count(list, &json) == 0) {
		(void) snprintf(errbuf,
		    sizeof (errbuf),
		    gettext("no pools available"));
		if (!json.json && !json.ld_json)
			printf("%s\n", errbuf);
		else
			fnvlist_add_string(json.nv_dict_error,
			    "error", errbuf);
		ret = 0;
	}
	pool_list_free(list, &json);
	zprop_free_list(cb.cb_proplist);
json_out :
	if (json.json || json.ld_json) {
		if (json.json) {
			if (timestamp_fmt != NODATE) {
				char *buff;
				buff = get_timestamp(timestamp_fmt);
				fnvlist_add_string(json.nv_dict_props,
				    "date", buff);
				free(buff);
			}
			fnvlist_add_string(json.nv_dict_props,
			    "schema_version", "1.0");
			fnvlist_add_nvlist_array(json.nv_dict_props,
			    "stdout",
			    (nvlist_t **)json.json_data, json.nb_elem);
			fnvlist_add_nvlist(json.nv_dict_props,
			    "stderr", json.nv_dict_error);
			nvlist_print_json(stdout, json.nv_dict_props);
		}
		if (json.ld_json)
			nvlist_print_json(stdout, json.nv_dict_error);
		while (((json.nb_elem)--) > 0)
			fnvlist_free(
			    ((nvlist_t **)
			    (json.json_data))[json.nb_elem]);
		free(json.json_data);
		fnvlist_free(json.nv_dict_error);
		fnvlist_free(json.nv_dict_props);
		printf("\n");
		fflush(stdout);
	}
	return (ret);

json_usage:
	if (!json.json && !json.ld_json)
		usage(B_FALSE);
	else {
		if (json.json) {
			fnvlist_add_string(json.nv_dict_props,
			    "schema_version", "1.0");
			fnvlist_add_nvlist_array(json.nv_dict_props,
			    "stdout",
			    (nvlist_t **)json.json_data, json.nb_elem);
			fnvlist_add_nvlist(json.nv_dict_props,
			    "stderr", json.nv_dict_error);
			nvlist_print_json(stdout, json.nv_dict_props);
		}
		if (json.ld_json)
			nvlist_print_json(stdout, json.nv_dict_error);
		fnvlist_free(json.nv_dict_error);
		fnvlist_free(json.nv_dict_props);
		fnvlist_free(json.nv_dict_buff);
		while (((json.nb_elem)--) > 0)
			fnvlist_free(
			    ((nvlist_t **)
			    (json.json_data))[json.nb_elem]);
		free(json.json_data);
	}
	printf("\n");
	fflush(stdout);
	exit(2);
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
	zfs_json_t json;
	json.json = json.ld_json = B_FALSE;
	char errbuf[1024];

	/* check options */
	while ((c = getopt(argc, argv, "Jjfo:")) != -1) {
		switch (c) {
		case 'j':
		case 'J':
			if (json.json)
				break;
			json.json = B_TRUE;
			json.nv_dict_props = fnvlist_alloc();
			json.nv_dict_buff = fnvlist_alloc();
			json.nv_dict_error = fnvlist_alloc();
			json.nb_elem = 0;
			json.json_data = NULL;
			if (replacing == B_TRUE)
				fnvlist_add_string(json.nv_dict_props,
				    "cmd", "zpool replace");
			else
				fnvlist_add_string(json.nv_dict_props,
				    "cmd", "zpool attach");
			fnvlist_add_string(json.nv_dict_error, "error", "");
			break;
		case 'f':
			force = B_TRUE;
			break;
		case 'o':
			if ((propval = strchr(optarg, '=')) == NULL) {
				(void) sprintf(errbuf, gettext("missing "
				    "'=' for -o option"));
				if (!json.json) {
					fprintf(stderr, "%s\n", errbuf);
					usage(B_FALSE);
				} else {
					fnvlist_add_string(json.nv_dict_error,
					    "error", errbuf);
					goto json_usage;
				}
			}
			*propval = '\0';
			propval++;

			if ((strcmp(optarg, ZPOOL_CONFIG_ASHIFT) != 0) ||
			    (add_prop_list(optarg, propval,
			    &props, B_TRUE, &json))) {
			    if (!json.json)
					usage(B_FALSE);
				else {
					fnvlist_add_string(json.nv_dict_error,
					    "error", "badusage");
					goto json_usage;
				}
			}
			break;
		case '?':
			(void) sprintf(errbuf, gettext("invalid option '%c"),
			    optopt);
			if (!json.json) {
				fprintf(stderr, "%s\n", errbuf);
				usage(B_FALSE);
			} else {
				fnvlist_add_string(json.nv_dict_error,
				    "error", errbuf);
				goto json_usage;
			}
		}
	}

	argc -= optind;
	argv += optind;

	/* get pool name and check number of arguments */
	if (argc < 1) {
		(void) sprintf(errbuf, gettext("missing pool name argument"));
		if (!json.json) {
			fprintf(stderr, "%s\n", errbuf);
			usage(B_FALSE);
		} else {
			fnvlist_add_string(json.nv_dict_error,
			    "error", errbuf);
			goto json_usage;
		}
	}

	poolname = argv[0];

	if (argc < 2) {
		(void) sprintf(errbuf,
		    gettext("missing <device> specification"));
		if (!json.json) {
			fprintf(stderr, "%s\n", errbuf);
			usage(B_FALSE);
		} else {
			fnvlist_add_string(json.nv_dict_error,
			    "error", errbuf);
			goto json_usage;
		}
	}

	old_disk = argv[1];

	if (argc < 3) {
		if (!replacing) {
			(void) sprintf(errbuf,
			    gettext("missing <new_device> specification"));
			if (!json.json) {
				fprintf(stderr, "%s\n", errbuf);
				usage(B_FALSE);
			} else {
				fnvlist_add_string(json.nv_dict_error,
				    "error", errbuf);
				goto json_usage;
			}
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
		(void) sprintf(errbuf, gettext("too many arguments"));
		if (!json.json) {
			fprintf(stderr, "%s\n", errbuf);
			usage(B_FALSE);
		} else {
			fnvlist_add_string(json.nv_dict_error,
			    "error", errbuf);
			goto json_usage;
		}
	}

	if ((zhp = zpool_open(&json, g_zfs, poolname)) == NULL) {
		ret = 1;
		goto json_out;
	}

	if (zpool_get_config(zhp, NULL) == NULL) {
		(void) sprintf(errbuf, gettext("pool '%s' is unavailable"),
		    poolname);
		if (!json.json)
			fprintf(stderr, "%s\n", errbuf);
		else
			fnvlist_add_string(json.nv_dict_error,
			    "error", errbuf);
		zpool_close(zhp);
		ret = 1;
		goto json_out;
	}

	nvroot = make_root_vdev(zhp, props, force, B_FALSE, replacing, B_FALSE,
	    argc, argv, &json);
	if (nvroot == NULL) {
		zpool_close(zhp);
		ret = 1;
		goto json_out;
	}

	ret = zpool_vdev_attach(zhp, old_disk,
	    new_disk, nvroot, replacing, &json);

	nvlist_free(nvroot);
	zpool_close(zhp);
json_out:
		if (json.json) {
		fnvlist_add_string(json.nv_dict_props, "schema_version", "1.0");
		fnvlist_add_nvlist_array(json.nv_dict_props, "stdout",
		    (nvlist_t **)json.json_data, json.nb_elem);
		fnvlist_add_nvlist(json.nv_dict_props,
		    "stderr", json.nv_dict_error);
		nvlist_print_json(stdout, json.nv_dict_props);
		fprintf(stdout, "\n");
		fflush(stdout);
		while (((json.nb_elem)--) > 0)
			fnvlist_free(
			    ((nvlist_t **)
			    (json.json_data))[json.nb_elem]);
		free(json.json_data);
		fnvlist_free(json.nv_dict_buff);
		fnvlist_free(json.nv_dict_error);
		fnvlist_free(json.nv_dict_props);
	}
	return (ret);
json_usage:
	if (json.json) {
		fnvlist_add_string(json.nv_dict_props, "schema_version", "1.0");
		fnvlist_add_nvlist_array(json.nv_dict_props, "stdout",
		    (nvlist_t **)json.json_data, json.nb_elem);
		fnvlist_add_nvlist(json.nv_dict_props,
		    "stderr", json.nv_dict_error);
		nvlist_print_json(stdout, json.nv_dict_props);
		fprintf(stdout, "\n");
		fflush(stdout);
		while (((json.nb_elem)--) > 0)
			fnvlist_free(
			    ((nvlist_t **)
			    (json.json_data))[json.nb_elem]);
		free(json.json_data);
		fnvlist_free(json.nv_dict_buff);
		fnvlist_free(json.nv_dict_error);
		fnvlist_free(json.nv_dict_props);
	}
	exit(2);
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
	zfs_json_t json;
	json.json = json.ld_json = B_FALSE;
	char errbuf[1024];


	/* check options */
	while ((c = getopt(argc, argv, "Jjf")) != -1) {
		switch (c) {
		case 'j':
		case 'J':
			if (json.json)
				break;
			json.json = B_TRUE;
			json.nv_dict_props = fnvlist_alloc();
			json.nv_dict_error = fnvlist_alloc();
			json.nb_elem = 0;
			json.json_data = NULL;
			fnvlist_add_string(json.nv_dict_props,
			    "cmd", "zpool detach");
			fnvlist_add_string(json.nv_dict_error, "error", "");
			break;
		case 'f':
		case '?':
			(void) sprintf(errbuf, gettext("invalid option '%c"),
			    optopt);
			if (json.json) {
				fnvlist_add_string(json.nv_dict_error,
				    "error", errbuf);
				goto json_usage;
			} else {
				fprintf(stderr, "%s\n", errbuf);
				usage(B_FALSE);
			}
		}
	}

	argc -= optind;
	argv += optind;

	/* get pool name and check number of arguments */
	if (argc < 1) {
		(void) sprintf(errbuf, gettext("missing pool name argument"));
		if (json.json) {
			fnvlist_add_string(json.nv_dict_error,
			    "error", errbuf);
			goto json_usage;
		} else {
			fprintf(stderr, "%s\n", errbuf);
			usage(B_FALSE);
		}
	}
	if (argc < 2) {
		(void) sprintf(errbuf,
		    gettext("missing <device> specification"));
		if (json.json) {
			fnvlist_add_string(json.nv_dict_error,
			    "error", errbuf);
			goto json_usage;
		} else {
			fprintf(stderr, "%s\n", errbuf);
			usage(B_FALSE);
		}
	}

	poolname = argv[0];
	path = argv[1];

	if ((zhp = zpool_open(&json, g_zfs, poolname)) == NULL) {
		ret = 1;
		goto json_out;
	}

	ret = zpool_vdev_detach(zhp, path, &json);

	zpool_close(zhp);

json_out:
		if (json.json) {
		fnvlist_add_string(json.nv_dict_props, "schema_version", "1.0");
		fnvlist_add_nvlist_array(json.nv_dict_props, "stdout",
		    (nvlist_t **)json.json_data, json.nb_elem);
		fnvlist_add_nvlist(json.nv_dict_props,
		    "stderr", json.nv_dict_error);
		nvlist_print_json(stdout, json.nv_dict_props);
		fprintf(stdout, "\n");
		fflush(stdout);
		while (((json.nb_elem)--) > 0)
			fnvlist_free(
			    ((nvlist_t **)
			    (json.json_data))[json.nb_elem]);
		free(json.json_data);
		fnvlist_free(json.nv_dict_error);
		fnvlist_free(json.nv_dict_props);
	}
	return (ret);
json_usage:
	if (json.json) {
		fnvlist_add_string(json.nv_dict_props, "schema_version", "1.0");
		fnvlist_add_nvlist_array(json.nv_dict_props, "stdout",
		    (nvlist_t **)json.json_data, json.nb_elem);
		fnvlist_add_nvlist(json.nv_dict_props,
		    "stderr", json.nv_dict_error);
		nvlist_print_json(stdout, json.nv_dict_props);
		fprintf(stdout, "\n");
		fflush(stdout);
		while (((json.nb_elem)--) > 0)
			fnvlist_free(
			    ((nvlist_t **)
			    (json.json_data))[json.nb_elem]);
		free(json.json_data);
		fnvlist_free(json.nv_dict_error);
		fnvlist_free(json.nv_dict_props);
	}
	exit(2);
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
	zfs_json_t json;
	json.json = json.ld_json = B_FALSE;
	char errbuf[1024];


	flags.dryrun = B_FALSE;
	flags.import = B_FALSE;
	flags.name_flags = 0;

	/* check options */
	while ((c = getopt(argc, argv, ":gLR:no:PJj")) != -1) {
		switch (c) {
		case 'g':
			flags.name_flags |= VDEV_NAME_GUID;
			break;
		case 'L':
			flags.name_flags |= VDEV_NAME_FOLLOW_LINKS;
		case 'j':
		case 'J':
			if (json.json)
				break;
			json.json = B_TRUE;
			json.nv_dict_props = fnvlist_alloc();
			json.nv_dict_error = fnvlist_alloc();
			json.nb_elem = 0;
			json.json_data = NULL;
			fnvlist_add_string(json.nv_dict_props,
			    "cmd", "zpool split");
			fnvlist_add_string(json.nv_dict_props,
			    "schema_version", "1.0");
			fnvlist_add_string(json.nv_dict_error,
			    "error", "");
			break;
		case 'R':
			flags.import = B_TRUE;
			if (add_prop_list(
			    zpool_prop_to_name(ZPOOL_PROP_ALTROOT), optarg,
			    &props, B_TRUE, &json) != 0) {
				if (props)
					nvlist_free(props);
				goto usage;
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
				    &props, B_TRUE, &json) != 0) {
					if (props)
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
			(void) sprintf(errbuf, gettext("missing argument for "
			    "'%c' option"), optopt);
			if (json.json)
				fnvlist_add_string(json.nv_dict_error,
				    "error", errbuf);
			else
				fprintf(stderr, "%s\n", errbuf);
			goto usage;
			break;
		case '?':
			(void) sprintf(errbuf, gettext("invalid option '%c'"),
			    optopt);
			if (json.json)
				fnvlist_add_string(json.nv_dict_error,
				    "error", errbuf);
			else
				fprintf(stderr, "%s\n", errbuf);
			goto usage;
			break;
		}
	}

	if (!flags.import && mntopts != NULL) {
		(void) sprintf(errbuf, gettext("setting mntopts is only "
		    "valid when importing the pool"));
		if (json.json)
				fnvlist_add_string(json.nv_dict_error,
				    "error", errbuf);
		else
			fprintf(stderr, "%s\n", errbuf);
		goto usage;
	}

	argc -= optind;
	argv += optind;

	if (argc < 1) {
		(void) sprintf(errbuf, gettext("Missing pool name"));
		if (json.json)
			fnvlist_add_string(json.nv_dict_error, "error", errbuf);
		else
			fprintf(stderr, "%s\n", errbuf);
		goto usage;
	}
	if (argc < 2) {
		(void) sprintf(errbuf, gettext("Missing new pool name"));
		if (json.json)
			fnvlist_add_string(json.nv_dict_error, "error", errbuf);
		else
			fprintf(stderr, "%s\n", errbuf);
		goto usage;
	}

	srcpool = argv[0];
	newpool = argv[1];

	argc -= 2;
	argv += 2;

	if ((zhp = zpool_open(&json, g_zfs, srcpool)) == NULL) {
		ret = 1;
		goto json_out;
	}

	config = split_mirror_vdev(zhp, newpool,
	    props, flags, argc, argv, &json);
	if (config == NULL) {
		ret = 1;
	} else {
		if (flags.dryrun && !json.json) {
			(void) printf(gettext("would create '%s' with the "
			    "following layout:\n\n"), newpool);
			print_vdev_tree(NULL, newpool, config, 0, B_FALSE,
			    flags.name_flags);
		}
		nvlist_free(config);
	}

	zpool_close(zhp);

	if (ret != 0 || flags.dryrun || !flags.import)
		goto json_out;

	/*
	 * The split was successful. Now we need to open the new
	 * pool and import it.
	 */
	if ((zhp = zpool_open_canfail(&json, g_zfs, newpool)) == NULL) {
		ret = 1;
		goto json_out;
	}
	if (zpool_get_state(zhp) != POOL_STATE_UNAVAIL &&
	    zpool_enable_datasets(zhp, mntopts, 0, &json) != 0) {
		ret = 1;
		if (!json.json) {
			(void) fprintf(stderr,
			    gettext("Split was successful, but "
			    "the datasets could not all be mounted\n"));
			(void) fprintf(stderr,
			    gettext("Try doing '%s' with a "
			    "different altroot\n"), "zpool import");
		} else {
			(void) sprintf(errbuf,
			    gettext("Split was successful, but "
			    "the datasets could not all be mounted, "
			    "Try doing '%s' with a "
			    "different altroot"), "zpool import");
			fnvlist_add_string(json.nv_dict_error, "error", errbuf);
		}
	}
	zpool_close(zhp);
json_out :
	if (json.json) {
		fnvlist_add_nvlist_array(json.nv_dict_props, "stdout",
		    (nvlist_t **)json.json_data, json.nb_elem);
		fnvlist_add_nvlist(json.nv_dict_props,
		    "stderr", json.nv_dict_error);
		nvlist_print_json(stdout, json.nv_dict_props);
		fprintf(stdout, "\n");
		fflush(stdout);
		fnvlist_free(json.nv_dict_error);
		fnvlist_free(json.nv_dict_props);
	}
	return (ret);
usage :
	if (json.json) {
		fnvlist_add_nvlist_array(json.nv_dict_props, "stdout",
		    (nvlist_t **)json.json_data, json.nb_elem);
		fnvlist_add_nvlist(json.nv_dict_props,
		    "stderr", json.nv_dict_error);
		nvlist_print_json(stdout, json.nv_dict_props);
		fprintf(stdout, "\n");
		fflush(stdout);
		fnvlist_free(json.nv_dict_error);
		fnvlist_free(json.nv_dict_props);
	} else
		usage(B_FALSE);
	exit(2);
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
	zfs_json_t json;
	json.json = json.ld_json = B_FALSE;
	char errbuf[1024];

	/* check options */
	while ((c = getopt(argc, argv, "etjJ")) != -1) {
		switch (c) {
		case 'j':
		case 'J':
			if (json.json)
				break;
			json.json = B_TRUE;
			json.nv_dict_props = fnvlist_alloc();
			json.nv_dict_error = fnvlist_alloc();
			json.nb_elem = 0;
			json.json_data = NULL;
			fnvlist_add_string(json.nv_dict_props,
			    "cmd", "zpool online");
			fnvlist_add_string(json.nv_dict_error, "error", "");
			break;
		case 'e':
			flags |= ZFS_ONLINE_EXPAND;
			break;
		case 't':
		case '?':
			(void) sprintf(errbuf, gettext("invalid option '%c'"),
			    optopt);
			if (!json.json) {
				fprintf(stderr, "%s\n", errbuf);
				usage(B_FALSE);
			} else {
				fnvlist_add_string(json.nv_dict_error,
				    "error", errbuf);
				goto json_usage;
			}
		}
	}

	argc -= optind;
	argv += optind;

	/* get pool name and check number of arguments */
	if (argc < 1) {
		(void) sprintf(errbuf, gettext("missing pool name"));
			if (!json.json) {
				fprintf(stderr, "%s\n", errbuf);
				usage(B_FALSE);
			} else {
				fnvlist_add_string(json.nv_dict_error,
				    "error", errbuf);
				goto json_usage;
			}
	}
	if (argc < 2) {
		(void) sprintf(errbuf, gettext("missing device name"));
			if (!json.json) {
				fprintf(stderr, "%s\n", errbuf);
				usage(B_FALSE);
			} else {
				fnvlist_add_string(json.nv_dict_error,
				    "error", errbuf);
				goto json_usage;
			}
	}

	poolname = argv[0];

	if ((zhp = zpool_open(&json, g_zfs,
	    poolname)) == NULL) {
		ret = 1;
		goto json_usage;
	}
	for (i = 1; i < argc; i++) {
		if (zpool_vdev_online(zhp, argv[i], flags,
		    &newstate, &json) == 0) {
			if (newstate != VDEV_STATE_HEALTHY) {
				if (!json.json) {
				(void) printf(gettext("warning: device '%s' "
				    "onlined, but remains in faulted state"),
				    argv[i]);
				if (newstate == VDEV_STATE_FAULTED)
					(void) printf(gettext("use 'zpool "
					    "clear' to restore a faulted "
					    "device\n"));
				else
					(void) printf(gettext("use 'zpool "
					    "replace' to replace devices "
					    "that are no longer present\n"));
			} else {
				if (newstate == VDEV_STATE_FAULTED) {
					(void) sprintf(errbuf,
					    gettext("warning: device '%s' "
					    "onlined, but remains in"
					    " faulted state, use 'zpool "
					    "clear' to restore a faulted "
					    "device"), argv[i]);
					fnvlist_add_string(json.nv_dict_error,
					    "error", errbuf);
				} else {
					(void) printf(gettext("warning:"
						" device '%s' "
					    "onlined, but remains"
					    " in faulted state,use 'zpool "
					    "replace' to replace devices "
					    "that are no longer present"),
					    argv[i]);
					fnvlist_add_string(json.nv_dict_error,
					    "error", errbuf);
				}
			}
		}
		} else {
			ret = 1;
		}
	}
if (json.json) {
		fnvlist_add_string(json.nv_dict_props, "schema_version", "1.0");
		fnvlist_add_nvlist_array(json.nv_dict_props, "stdout",
		    (nvlist_t **)json.json_data, json.nb_elem);
		fnvlist_add_nvlist(json.nv_dict_props,
		    "stderr", json.nv_dict_error);
		nvlist_print_json(stdout, json.nv_dict_props);
		fprintf(stdout, "\n");
		fflush(stdout);
		while (((json.nb_elem)--) > 0)
			fnvlist_free(
			    ((nvlist_t **)
			    (json.json_data))[json.nb_elem]);
		free(json.json_data);
		fnvlist_free(json.nv_dict_error);
		fnvlist_free(json.nv_dict_props);
	}
	zpool_close(zhp);

	return (ret);
	json_usage:
	if (json.json) {
		fnvlist_add_string(json.nv_dict_props, "schema_version", "1.0");
		fnvlist_add_nvlist_array(json.nv_dict_props, "stdout",
		    (nvlist_t **)json.json_data, json.nb_elem);
		fnvlist_add_nvlist(json.nv_dict_props,
		    "stderr", json.nv_dict_error);
		nvlist_print_json(stdout, json.nv_dict_props);
		fprintf(stdout, "\n");
		fflush(stdout);
		while (((json.nb_elem)--) > 0)
			fnvlist_free(
			    ((nvlist_t **)
			    (json.json_data))[json.nb_elem]);
		free(json.json_data);
		fnvlist_free(json.nv_dict_error);
		fnvlist_free(json.nv_dict_props);
	}
	exit(2);
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
	zfs_json_t json;
	json.json = json.ld_json = B_FALSE;
	char errbuf[1024];

	/* check options */
	while ((c = getopt(argc, argv, "Jjft")) != -1) {
		switch (c) {
		case 'j':
		case 'J':
			if (json.json)
				break;
			json.json = B_TRUE;
			json.nv_dict_props = fnvlist_alloc();
			json.nv_dict_error = fnvlist_alloc();
			json.nb_elem = 0;
			json.json_data = NULL;
			fnvlist_add_string(json.nv_dict_props,
			    "cmd", "zpool offline");
			fnvlist_add_string(json.nv_dict_error, "error", "");
		break;
		case 't':
			istmp = B_TRUE;
			break;
		case 'f':
		case '?':
			(void) sprintf(errbuf, gettext("invalid option '%c'"),
			    optopt);
			if (!json.json) {
				fprintf(stderr, "%s\n", errbuf);
				usage(B_FALSE);
			} else {
				fnvlist_add_string(json.nv_dict_error,
				    "error", errbuf);
				goto json_usage;
			}
		}
	}

	argc -= optind;
	argv += optind;

	/* get pool name and check number of arguments */
	if (argc < 1) {
		(void) sprintf(errbuf, gettext("missing pool name"));
			if (!json.json) {
				fprintf(stderr, "%s\n", errbuf);
				usage(B_FALSE);
			} else {
				fnvlist_add_string(json.nv_dict_error,
				    "error", errbuf);
				goto json_usage;
			}
	}
	if (argc < 2) {
		(void) sprintf(errbuf, gettext("missing device name"));
			if (!json.json) {
				fprintf(stderr, "%s\n", errbuf);
				usage(B_FALSE);
			} else {
				fnvlist_add_string(json.nv_dict_error,
				    "error", errbuf);
				goto json_usage;
			}
	}

	poolname = argv[0];

	if ((zhp = zpool_open(&json, g_zfs, poolname)) == NULL) {
		if (!json.json)
			return (1);
		else {
			ret = 1;
			goto json_out;
		}
	}
	for (i = 1; i < argc; i++) {
		if (zpool_vdev_offline(zhp, argv[i], istmp, &json) != 0)
			ret = 1;
	}

	zpool_close(zhp);

json_out:
	if (json.json) {
		fnvlist_add_string(json.nv_dict_props, "schema_version", "1.0");
		fnvlist_add_nvlist_array(json.nv_dict_props, "stdout",
		    (nvlist_t **)json.json_data, json.nb_elem);
		fnvlist_add_nvlist(json.nv_dict_props,
		    "stderr", json.nv_dict_error);
		nvlist_print_json(stdout, json.nv_dict_props);
		fprintf(stdout, "\n");
		fflush(stdout);
		while (((json.nb_elem)--) > 0)
			fnvlist_free(
			    ((nvlist_t **)
			    (json.json_data))[json.nb_elem]);
		free(json.json_data);
		fnvlist_free(json.nv_dict_error);
		fnvlist_free(json.nv_dict_props);
	}

	return (ret);

json_usage:
	if (json.json) {
		fnvlist_add_string(json.nv_dict_props, "schema_version", "1.0");
		fnvlist_add_nvlist_array(json.nv_dict_props, "stdout",
		    (nvlist_t **)json.json_data, json.nb_elem);
		fnvlist_add_nvlist(json.nv_dict_props,
		    "stderr", json.nv_dict_error);
		nvlist_print_json(stdout, json.nv_dict_props);
		fprintf(stdout, "\n");
		fflush(stdout);
		while (((json.nb_elem)--) > 0)
			fnvlist_free(
			    ((nvlist_t **)
			    (json.json_data))[json.nb_elem]);
		free(json.json_data);
		fnvlist_free(json.nv_dict_error);
		fnvlist_free(json.nv_dict_props);
	}
	exit(2);
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
		zfs_json_t json;
	json.json = json.ld_json = B_FALSE;
	char errbuf[1024];

	/* check options */
	while ((c = getopt(argc, argv, "jJFnX")) != -1) {
		switch (c) {
		case 'j':
		case 'J':
			if (json.json)
				break;
			json.json = B_TRUE;
			json.nv_dict_props = fnvlist_alloc();
			json.nv_dict_error = fnvlist_alloc();
			json.nb_elem = 0;
			json.json_data = NULL;
			fnvlist_add_string(json.nv_dict_props,
			    "cmd", "zpool clear");
			fnvlist_add_string(json.nv_dict_error, "error", "");
			break;
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
			(void) sprintf(errbuf, gettext("invalid option '%c'"),
			    optopt);
			if (!json.json) {
				fprintf(stderr, "%s\n", errbuf);
				usage(B_FALSE);
			} else {
				fnvlist_add_string(json.nv_dict_error,
				    "error", errbuf);
				goto json_usage;
			}
		}
	}

	argc -= optind;
	argv += optind;

	if (argc < 1) {
		(void) sprintf(errbuf, gettext("missing pool name"));
		if (!json.json) {
			fprintf(stderr, "%s\n", errbuf);
			usage(B_FALSE);
		} else {
			fnvlist_add_string(json.nv_dict_error,
			    "error", errbuf);
			goto json_usage;
		}
	}

	if (argc > 2) {
		(void) sprintf(errbuf, gettext("too many arguments"));
		if (!json.json) {
			fprintf(stderr, "%s\n", errbuf);
			usage(B_FALSE);
		} else {
			fnvlist_add_string(json.nv_dict_error,
			    "error", errbuf);
			goto json_usage;
		}
	}

	if ((dryrun || xtreme_rewind) && !do_rewind) {
		(void) sprintf(errbuf,
		    gettext("-n or -X only meaningful with -F"));
		if (!json.json) {
			fprintf(stderr, "%s\n", errbuf);
			usage(B_FALSE);
		} else {
			fnvlist_add_string(json.nv_dict_error,
			    "error", errbuf);
			goto json_usage;
		}
	}
	if (dryrun)
		rewind_policy = ZPOOL_TRY_REWIND;
	else if (do_rewind)
		rewind_policy = ZPOOL_DO_REWIND;
	if (xtreme_rewind)
		rewind_policy |= ZPOOL_EXTREME_REWIND;

	/* In future, further rewind policy choices can be passed along here */
	if (nvlist_alloc(&policy, NV_UNIQUE_NAME, 0) != 0 ||
	    nvlist_add_uint32(policy,
	    ZPOOL_REWIND_REQUEST, rewind_policy) != 0) {
		ret = 1;
		goto json_out;
	}

	pool = argv[0];
	device = argc == 2 ? argv[1] : NULL;

	if ((zhp = zpool_open_canfail(&json, g_zfs, pool)) == NULL) {
		nvlist_free(policy);
		ret = 1;
		goto json_out;
	}

	if (zpool_clear(zhp, device, policy, &json) != 0)
		ret = 1;

	zpool_close(zhp);

	nvlist_free(policy);


json_out:
	if (json.json) {
		fnvlist_add_string(json.nv_dict_props, "schema_version", "1.0");
		fnvlist_add_nvlist_array(json.nv_dict_props, "stdout",
		    (nvlist_t **)json.json_data, json.nb_elem);
		fnvlist_add_nvlist(json.nv_dict_props,
		    "stderr", json.nv_dict_error);
		nvlist_print_json(stdout, json.nv_dict_props);
		fprintf(stdout, "\n");
		fflush(stdout);
		while (((json.nb_elem)--) > 0)
			fnvlist_free(
			    ((nvlist_t **)
			    (json.json_data))[json.nb_elem]);
		free(json.json_data);
		fnvlist_free(json.nv_dict_error);
		fnvlist_free(json.nv_dict_props);
	}

	return (ret);

json_usage:
	if (json.json) {
		fnvlist_add_string(json.nv_dict_props, "schema_version", "1.0");
		fnvlist_add_nvlist_array(json.nv_dict_props, "stdout",
		    (nvlist_t **)json.json_data, json.nb_elem);
		fnvlist_add_nvlist(json.nv_dict_props,
		    "stderr", json.nv_dict_error);
		nvlist_print_json(stdout, json.nv_dict_props);
		fprintf(stdout, "\n");
		fflush(stdout);
		while (((json.nb_elem)--) > 0)
			fnvlist_free(
			    ((nvlist_t **)
			    (json.json_data))[json.nb_elem]);
		free(json.json_data);
		fnvlist_free(json.nv_dict_error);
		fnvlist_free(json.nv_dict_props);
	}
	exit(2);
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
	if ((zhp = zpool_open(NULL, g_zfs, poolname)) == NULL)
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
	zfs_json_t json;
	json.json = json.ld_json = B_FALSE;
	char errbuf[1024];

	/* check options */
	while ((c = getopt(argc, argv, "Jj")) != -1) {
		switch (c) {
		case 'j':
		case 'J':
			if (json.json)
				break;
			json.json = B_TRUE;
			json.nv_dict_props = fnvlist_alloc();
			json.nv_dict_error = fnvlist_alloc();
			json.nb_elem = 0;
			json.json_data = NULL;
			fnvlist_add_string(json.nv_dict_props,
			    "cmd", "zpool reopen");
			fnvlist_add_string(json.nv_dict_error,
			    "error", "");
			fnvlist_add_string(json.nv_dict_props,
			    "schema_version", "1.0");

			break;
		case '?':
			(void) sprintf(errbuf, gettext("invalid option '%c'"),
			    optopt);
			if (!json.json)
				(void) fprintf(stderr, "%s\n", errbuf);
			else {
				fnvlist_add_string(json.nv_dict_error,
				    "error", errbuf);
				goto json_usage;
			}
			usage(B_FALSE);
		}
	}

	argc -= optind;
	argv += optind;

	if (argc < 1) {
		(void) sprintf(errbuf, gettext("missing pool name"));
		if (!json.json)
				(void) fprintf(stderr, "%s\n", errbuf);
		else {
			fnvlist_add_string(json.nv_dict_error, "error", errbuf);
			goto json_usage;
		}
		usage(B_FALSE);
	}
	if (argc > 2) {
		(void) sprintf(errbuf, gettext("too many arguments"));
		if (!json.json)
				(void) fprintf(stderr, "%s\n", errbuf);
		else {
			fnvlist_add_string(json.nv_dict_error, "error", errbuf);
			goto json_usage;
		}
		usage(B_FALSE);
	}

	pool = argv[0];

	if ((zhp = zpool_open_canfail(&json, g_zfs, pool)) == NULL) {
		ret = 1;
		goto json_out;
	}
	ret = zpool_reopen(zhp, &json);
	zpool_close(zhp);
json_out:
	if (json.json) {
		fnvlist_add_nvlist_array(json.nv_dict_props, "stdout",
		    (nvlist_t **)json.json_data, json.nb_elem);
		fnvlist_add_nvlist(json.nv_dict_props,
		    "stderr", json.nv_dict_error);
		nvlist_print_json(stdout, json.nv_dict_props);
		fprintf(stdout, "\n");
		fflush(stdout);
		while (((json.nb_elem)--) > 0)
			fnvlist_free(
			    ((nvlist_t **)
			    (json.json_data))[json.nb_elem]);
		free(json.json_data);
		fnvlist_free(json.nv_dict_error);
		fnvlist_free(json.nv_dict_props);
	}
	return (ret);
json_usage:
	if (json.json) {
		fnvlist_add_nvlist_array(json.nv_dict_props, "stdout",
		    (nvlist_t **)json.json_data, json.nb_elem);
		fnvlist_add_nvlist(json.nv_dict_props,
		    "stderr", json.nv_dict_error);
		nvlist_print_json(stdout, json.nv_dict_props);
		fprintf(stdout, "\n");
		fflush(stdout);
		while (((json.nb_elem)--) > 0)
			fnvlist_free(
			    ((nvlist_t **)
			    (json.json_data))[json.nb_elem]);
		free(json.json_data);
		fnvlist_free(json.nv_dict_error);
		fnvlist_free(json.nv_dict_props);
	}
	exit(2);
}

typedef struct scrub_cbdata {
	int	cb_type;
	int	cb_argc;
	char	**cb_argv;
} scrub_cbdata_t;

int
scrub_callback(zpool_handle_t *zhp, void *data, zfs_json_t *json)
{
	scrub_cbdata_t *cb = data;
	int err;
	char errbuf[1024];

	/*
	 * Ignore faulted pools.
	 */
	if (zpool_get_state(zhp) == POOL_STATE_UNAVAIL) {
		(void) snprintf(errbuf, sizeof (errbuf),
		    gettext("cannot scrub '%s': pool is "
		    "currently unavailable"), zpool_get_name(zhp));
		if (!json->json)
			fprintf(stderr, "%s\n", errbuf);
		else
			fnvlist_add_string(json->nv_dict_error,
			    "error", errbuf);
		return (1);
	}

	err = zpool_scan(zhp, cb->cb_type, json);

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
	int ret = 0;

	cb.cb_type = POOL_SCAN_SCRUB;
	zfs_json_t json;
	json.json = json.ld_json = B_FALSE;
	char errbuf[1024];

	/* check options */
	while ((c = getopt(argc, argv, "sjJ")) != -1) {
		switch (c) {
		case 'j':
		case 'J':
			if (json.json)
				break;
			json.json = B_TRUE;
			json.nv_dict_props = fnvlist_alloc();
			json.nv_dict_error = fnvlist_alloc();
			json.nb_elem = 0;
			json.json_data = NULL;
			fnvlist_add_string(json.nv_dict_props,
			    "cmd", "zpool scrub");
			fnvlist_add_string(json.nv_dict_error, "error", "");
			break;
		case 's':
			cb.cb_type = POOL_SCAN_NONE;
			break;
		case '?':
			(void) snprintf(errbuf, sizeof (errbuf),
			    gettext("invalid option '%c'"),
			    optopt);
			if (!json.json)
				fprintf(stderr, "%s\n", errbuf);
			else
				fnvlist_add_string(json.nv_dict_error,
				    "error", errbuf);
			goto json_usage;
		}
	}

	cb.cb_argc = argc;
	cb.cb_argv = argv;
	argc -= optind;
	argv += optind;

	if (argc < 1) {
		(void) snprintf(errbuf, sizeof (errbuf),
		    gettext("missing pool name argument"));
		if (!json.json)
			fprintf(stderr, "%s\n", errbuf);
		else
			fnvlist_add_string(json.nv_dict_error,
			    "error", errbuf);
		goto json_usage;
	}
	ret = for_each_pool(argc, argv,
	    B_TRUE, NULL, scrub_callback, &cb, &json);
	if (json.json) {
		fnvlist_add_string(json.nv_dict_props, "schema_version", "1.0");
		fnvlist_add_nvlist_array(json.nv_dict_props, "stdout",
		    (nvlist_t **)json.json_data, json.nb_elem);
		fnvlist_add_nvlist(json.nv_dict_props,
		    "stderr", json.nv_dict_error);
		nvlist_print_json(stdout, json.nv_dict_props);
		fprintf(stdout, "\n");
		fflush(stdout);
		while (((json.nb_elem)--) > 0)
			fnvlist_free(
			    ((nvlist_t **)
			    (json.json_data))[json.nb_elem]);
		free(json.json_data);
		fnvlist_free(json.nv_dict_error);
		fnvlist_free(json.nv_dict_props);
	}

	return (ret);

	json_usage:
	if (!json.json)
		usage(B_FALSE);
	else {
		fnvlist_add_string(json.nv_dict_props, "schema_version", "1.0");
		fnvlist_add_nvlist_array(json.nv_dict_props, "stdout",
		    (nvlist_t **)json.json_data, json.nb_elem);
		fnvlist_add_nvlist(json.nv_dict_props,
		    "stderr", json.nv_dict_error);
		nvlist_print_json(stdout, json.nv_dict_props);
		fprintf(stdout, "\n");
		fflush(stdout);
		while (((json.nb_elem)--) > 0)
			fnvlist_free(
			    ((nvlist_t **)
			    (json.json_data))[json.nb_elem]);
		free(json.json_data);
		fnvlist_free(json.nv_dict_error);
		fnvlist_free(json.nv_dict_props);
	}
	exit(2);

}

typedef struct status_cbdata {
	int		cb_count;
	int		cb_name_flags;
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
print_scan_status(pool_scan_stat_t *ps, zfs_json_t *json)
{
	time_t start, end;
	uint64_t elapsed, mins_left, hours_left;
	uint64_t pass_exam, examined, total;
	uint_t rate;
	double fraction_done;
	char processed_buf[7], examined_buf[7], total_buf[7], rate_buf[7];
	char errbuf[1024];

	if (!json->json && !json->ld_json)
		(void) printf(gettext("  scan: "));

	/* If there's never been a scan, there's not much to say. */
	if (ps == NULL || ps->pss_func == POOL_SCAN_NONE ||
	    ps->pss_func >= POOL_SCAN_FUNCS) {
		if (!json->json && !json->ld_json)
			(void) printf(gettext("none requested\n"));
		else
			fnvlist_add_string(json->nv_dict_buff,
			    "SCAN", "none requested");
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

		if (ps->pss_func == POOL_SCAN_SCRUB) {
				(void) snprintf(errbuf, sizeof (errbuf),
				    gettext("scrub repaired %s"
				    " in %lluh%um with "
			    "%llu errors on %s"), processed_buf,
			    (u_longlong_t)(minutes_taken / 60),
			    (uint_t)(minutes_taken % 60),
			    (u_longlong_t)ps->pss_errors,
			    ctime((time_t *)&end));
		} else if (ps->pss_func == POOL_SCAN_RESILVER) {
			(void) snprintf(errbuf,
			    sizeof (errbuf), gettext("resilvered %s "
			    "in %lluh%um with "
			    "%llu errors on %s"), processed_buf,
			    (u_longlong_t)(minutes_taken / 60),
			    (uint_t)(minutes_taken % 60),
			    (u_longlong_t)ps->pss_errors,
			    ctime((time_t *)&end));
		}
		/* LINTED */
		if (!json->json && !json->ld_json) {
			printf("%s\n", errbuf);
		} else {
			fnvlist_add_string(json->nv_dict_buff, "SCAN", errbuf);
		}
		return;
	} else if (ps->pss_state == DSS_CANCELED) {
		if (ps->pss_func == POOL_SCAN_SCRUB) {
			(void) snprintf(errbuf, sizeof (errbuf),
			    gettext("scrub canceled on %s"),
			    ctime(&end));
			if (!json->json && !json->ld_json)
				printf("%s\n", errbuf);
			else
				fnvlist_add_string(json->nv_dict_buff,
				    "SCAN", errbuf);
		} else if (ps->pss_func == POOL_SCAN_RESILVER) {
			(void) snprintf(errbuf, sizeof (errbuf),
			    gettext("resilver canceled on %s"),
			    ctime(&end));
			if (!json->json && !json->ld_json)
				printf("%s\n", errbuf);
			else
				fnvlist_add_string(json->nv_dict_buff,
				    "SCAN", errbuf);
		}
		return;
	}

	assert(ps->pss_state == DSS_SCANNING);

	/*
	 * Scan is in progress.
	 */
	if (ps->pss_func == POOL_SCAN_SCRUB) {
		(void) snprintf(errbuf, sizeof (errbuf),
		    gettext("scrub in progress since %s"),
		    ctime(&start));
		if (!json->json && !json->ld_json)
				printf("%s\n", errbuf);
	} else if (ps->pss_func == POOL_SCAN_RESILVER) {
		(void) snprintf(errbuf, sizeof (errbuf),
		    gettext("resilver in progress since %s"),
		    ctime(&start));
		if (!json->json && !json->ld_json)
				printf("%s\n", errbuf);
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
	if (!json->json && !json->ld_json)
		(void) printf(gettext("    %s scanned out of %s at %s/s"),
		    examined_buf, total_buf, rate_buf);
	else
		snprintf(errbuf, sizeof (errbuf),
		    gettext("    %s scanned out of %s at %s/s"),
		    examined_buf, total_buf, rate_buf);
	if (hours_left < (30 * 24)) {
		if (!json->json && !json->ld_json)
			(void) printf(gettext(", %lluh%um to go\n"),
			    (u_longlong_t)hours_left, (uint_t)(mins_left % 60));
		else {
			(void) snprintf(errbuf, sizeof (errbuf),
				gettext("%s, %lluh%um to go"),
			    errbuf, (u_longlong_t)hours_left,
			    (uint_t)(mins_left % 60));
		}

	} else {
		if (!json->json && !json->ld_json)
		(void) printf(gettext(
		    ", (scan is slow, no estimated time)\n"));
		else
			(void) snprintf(errbuf, sizeof (errbuf),
				gettext(", (scan is slow, no estimated time)"));

	}

	if (ps->pss_func == POOL_SCAN_RESILVER) {
		if (!json->json && !json->ld_json)
			(void) printf(gettext("    %s "
			    "resilvered, %.2f%% done\n"),
			    processed_buf, 100 * fraction_done);
		else {
			(void) snprintf(errbuf, sizeof (errbuf),
			    gettext("%s  %s resilvered, %.2f%% done"),
			    errbuf, processed_buf, 100 * fraction_done);
			fnvlist_add_string(json->nv_dict_buff, "SCAN", errbuf);
		}
	} else if (ps->pss_func == POOL_SCAN_SCRUB) {
		if (!json->json && !json->ld_json)
			(void) printf(gettext("    %s repaired, %.2f%% done\n"),
			    processed_buf, 100 * fraction_done);
		else {
			(void) snprintf(errbuf, sizeof (errbuf),
			    gettext("%s     %s repaired, %.2f%% done"),
			    errbuf, processed_buf, 100 * fraction_done);
			fnvlist_add_string(json->nv_dict_buff, "SCAN", errbuf);
		}
	}
}

static void
print_error_log(zpool_handle_t *zhp, zfs_json_t *json)
{
	nvlist_t *nverrlist = NULL;
	nvpair_t *elem;
	char *pathname;
	size_t len = MAXPATHLEN * 2;
	char errbuf[1024];

	if (zpool_get_errlog(zhp, &nverrlist) != 0) {
		if (!json || (!json->json && !json->ld_json))
			(void) printf("errors: List of errors unavailable "
			    "(insufficient privileges)\n");
		else
			fnvlist_add_string(json->nv_dict_buff, "errors",
			    "List of errors unavailable "
			    "(insufficient privileges)");
		return;
	}

	if (!json || (!json->json && !json->ld_json))
		(void) printf("errors: Permanent errors have been "
		    "detected in the following files:\n\n");
	else
		(void) snprintf(errbuf, sizeof (errbuf),
			"Permanent errors have been "
		    "detected in the following files:");

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
		zpool_obj_to_path(zhp, dsobj, obj, pathname, len, json);
		if (!json || (!json->json && !json->ld_json))
			(void) printf("%7s %s\n", "", pathname);
		else
			(void) snprintf(errbuf, sizeof (errbuf),
				"%s %s\n", errbuf, pathname);
	}
	free(pathname);
	nvlist_free(nverrlist);
	if (json && (json->json || json->ld_json))
		fnvlist_add_string(json->nv_dict_buff, "errors",
		    errbuf);
}

static void
print_spares(zpool_handle_t *zhp, nvlist_t **spares, uint_t nspares,
    int namewidth, int name_flags)
{
	uint_t i;
	char *name;

	if (nspares == 0)
		return;

	(void) printf(gettext("\tspares\n"));

	for (i = 0; i < nspares; i++) {
		name = zpool_vdev_name(g_zfs, zhp, spares[i], name_flags);
		print_status_config(zhp, name, spares[i],
		    namewidth, 2, B_TRUE, name_flags, NULL, NULL);
		free(name);
	}
}

static void
print_l2cache(zpool_handle_t *zhp, nvlist_t **l2cache, uint_t nl2cache,
    int namewidth, int name_flags)
{
	uint_t i;
	char *name;

	if (nl2cache == 0)
		return;

	(void) printf(gettext("\tcache\n"));

	for (i = 0; i < nl2cache; i++) {
		name = zpool_vdev_name(g_zfs, zhp, l2cache[i], name_flags);
		print_status_config(zhp, name, l2cache[i],
		    namewidth, 2, B_FALSE, name_flags, NULL, NULL);
		free(name);
	}
}

static void
print_dedup_stats(nvlist_t *config, zfs_json_t *json)
{
	ddt_histogram_t *ddh;
	ddt_stat_t *dds;
	ddt_object_t *ddo;
	uint_t c;
	char errbuf[1024];

	/*
	 * If the pool was faulted then we may not have been able to
	 * obtain the config. Otherwise, if we have anything in the dedup
	 * table continue processing the stats.
	 */
	if (nvlist_lookup_uint64_array(config, ZPOOL_CONFIG_DDT_OBJ_STATS,
	    (uint64_t **)&ddo, &c) != 0)
		return;
	if (!json->json && !json->ld_json) {
		(void) printf("\n");
		(void) printf(gettext(" dedup: "));
	}
	if (ddo->ddo_count == 0) {
		(void) snprintf(errbuf,
		    sizeof (errbuf),
		    gettext("no DDT entries"));
		if (!json->json && !json->ld_json)
			printf("%s\n", errbuf);
		else
			fnvlist_add_string(json->nv_dict_buff,
			    "dedup", errbuf);
		return;
	}

	(void) snprintf(errbuf,
	    sizeof (errbuf),
	    "DDT entries %llu,"
	    "size %llu on disk, %llu in core\n",
	    (u_longlong_t)ddo->ddo_count,
	    (u_longlong_t)ddo->ddo_dspace,
	    (u_longlong_t)ddo->ddo_mspace);
	if (!json->json && !json->ld_json)
		printf("%s\n", errbuf);
	else
		fnvlist_add_string(json->nv_dict_buff,
		    "dedup", errbuf);

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
status_callback(zpool_handle_t *zhp, void *data, zfs_json_t *json)
{
	status_cbdata_t *cbp = data;
	nvlist_t *config, *nvroot;
	char *msgid;
	zpool_status_t reason;
	zpool_errata_t errata;
	const char *health;
	char errbuf[1024];
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
			(void) snprintf(errbuf, sizeof (errbuf),
			    gettext("pool '%s' is healthy"),
			    zpool_get_name(zhp));
			if (!json->json && !json->ld_json)
				fprintf(stderr, "%s\n", errbuf);
			else
				fnvlist_add_string(json->nv_dict_error,
				    "error", errbuf);
			if (cbp->cb_first)
				cbp->cb_first = B_FALSE;
		}
		return (0);
	}
	if (cbp->cb_first && !json->json && !json->ld_json) {
		cbp->cb_first = B_FALSE;
	} else if (!json ||(!json->json && !json->ld_json)) {
		(void) printf("\n");
	}
	if (json->json || json->ld_json) {
		json->nv_dict_buff = fnvlist_alloc();
		json->nv_dict_buff_cpy = fnvlist_alloc();
	}
	verify(nvlist_lookup_nvlist(config, ZPOOL_CONFIG_VDEV_TREE,
	    &nvroot) == 0);
	verify(nvlist_lookup_uint64_array(nvroot, ZPOOL_CONFIG_VDEV_STATS,
	    (uint64_t **)&vs, &c) == 0);
	health = zpool_state_to_name(vs->vs_state, vs->vs_aux);
	if (!json->json && !json->ld_json) {
		(void) printf(gettext("  pool: %s\n"), zpool_get_name(zhp));
		(void) printf(gettext(" state: %s\n"), health);
	} else {
		if (json->ld_json && json->timestamp)
			fnvlist_add_string(json->nv_dict_buff, "date",
			    fnvlist_lookup_string(json->nv_dict_props,
			    "date"));
		fnvlist_add_string(
		    json->nv_dict_buff,
		    "pool", zpool_get_name(zhp));
		fnvlist_add_string(
		    json->nv_dict_buff,
		    "state", health);
	}
	switch (reason) {
	case ZPOOL_STATUS_MISSING_DEV_R:
		if (!json->json && !json->ld_json) {
			(void) printf(gettext("status: One or more "
				"devices could not be opened.  Sufficient"
				" replicas exist for\n\tthe pool to "
			    "continue functioning in a degraded state.\n"));
			(void) printf(gettext("action: Attach"
			    " the missing device and "
			    "online it using 'zpool online'.\n"));
		} else {
			fnvlist_add_string(
			    json->nv_dict_buff,
			    "status", gettext("One or more devices could not "
		    "be opened.  Sufficient replicas exist for the pool to "
		    "continue functioning in a degraded state."));
			fnvlist_add_string(
			    json->nv_dict_buff,
		    "action", gettext("Attach the missing device and "
		    "online it using 'zpool online'."));
		}
		break;

	case ZPOOL_STATUS_MISSING_DEV_NR:
		if (!json->json && !json->ld_json) {
			(void) printf(gettext("status:"
				" One or more devices could not "
			    "be opened.  There are insufficient"
			    "\n\treplicas for the "
			    "pool to continue functioning.\n"));
			(void) printf(gettext("action:"
				" Attach the missing device and "
			    "online it using 'zpool online'.\n"));
		} else {
			fnvlist_add_string(
			    json->nv_dict_buff,
			    "status", gettext("status: One or"
			    " more devices could not "
			    "be opened.  There are insufficient"
			    " replicas for the "
			    "pool to continue functioning."));
			fnvlist_add_string(
			    json->nv_dict_buff,
			    "action", gettext("Attach the missing device and "
			    "online it using 'zpool online'."));
		}
		break;

	case ZPOOL_STATUS_CORRUPT_LABEL_R:
		if (!json->json && !json->ld_json) {
			(void) printf(gettext("status:"
				" One or more devices could not "
			    "be used because the label is"
			    " missing or\n\tinvalid.  "
			    "Sufficient replicas exist for the"
			    " pool to continue\n\t"
			    "functioning in a degraded state.\n"));
			(void) printf(gettext("action:"
				" Replace the device using "
			    "'zpool replace'.\n"));
		} else {
			fnvlist_add_string(
			    json->nv_dict_buff,
			    "status", gettext("One or more devices could not "
			    "be used because the label is missing or invalid.  "
			    "Sufficient replicas exist for the pool to continue"
			    "functioning in a degraded state."));
			fnvlist_add_string(
			    json->nv_dict_buff,
			    "action", gettext("Replace the device using "
			    "'zpool replace'."));
		}
		break;

	case ZPOOL_STATUS_CORRUPT_LABEL_NR:
		if (!json->json && !json->ld_json) {
			(void) printf(gettext("status: One"
				" or more devices could not "
			    "be used because the label is"
			    " missing \n\tor invalid.  "
			    "There are insufficient replicas for the pool to "
			    "continue\n\tfunctioning.\n"));
			zpool_explain_recover(zpool_get_handle(zhp),
			    zpool_get_name(zhp), reason, config, json);
		} else {
			(void) snprintf(errbuf, sizeof (errbuf),
			    gettext("status: One or more devices "
			    "could not "
			    "be used because the label is missing or invalid.  "
			    "There are insufficient replicas for the pool to "
			    "continue functioning."));
			zpool_explain_recover(zpool_get_handle(zhp),
			    zpool_get_name(zhp), reason, config, json);
			fnvlist_add_string(
			    json->nv_dict_buff,
			    "status", errbuf);
		}
		break;

	case ZPOOL_STATUS_FAILING_DEV:
		if (!json->json && !json->ld_json) {
			(void) printf(gettext("status: One or"
				" more devices has experienced an"
				" unrecoverable error. An\n\tattempt"
				" was made to correct the error.  "
				"Applications are unaffected.\n"));
			(void) printf(gettext("action: Determine if"
				" the device needs "
			    "to be replaced, and clear the errors\n\tusing "
			    "'zpool clear' or replace the device with 'zpool "
			    "replace'.\n"));
		} else {
			(void) fnvlist_add_string(
			    json->nv_dict_buff,
			    "status",
			    gettext("One or more devices has "
			    "experienced an unrecoverable "
			    "error.  An attempt was "
			    "made to correct the error.  Applications are "
			    "unaffected."));
			(void) fnvlist_add_string(
			    json->nv_dict_buff,
			    "action",
			    gettext("Determine if the device needs "
			    "to be replaced, and clear the errors using "
			    "'zpool clear' or replace the device with 'zpool "
			    "replace'."));
		}
		break;

	case ZPOOL_STATUS_OFFLINE_DEV:
		if (!json->json && !json->ld_json) {
			(void) printf(gettext("status: One or more devices has "
			    "been taken offline by the "
			    "administrator.\n\tSufficient "
			    "replicas exist for the pool to"
			    " continue functioning in "
			    "a\n\tdegraded state.\n"));
			(void) printf(gettext("action: Online the device using "
			    "'zpool online' or replace the"
			    " device with\n\t'zpool "
			    "replace'.\n"));
		} else {
			(void) fnvlist_add_string(
			    json->nv_dict_buff,
			    "status",
			    gettext("One or more devices has "
			    "been taken offline by the"
			    " administrator.Sufficient "
			    "replicas exist for the pool"
			    " to continue functioning in "
			    "a degraded state."));
			(void) fnvlist_add_string(
			    json->nv_dict_buff,
			    "action",
			    gettext("Online the device using "
			    "'zpool online' or replace the device with'zpool "
			    "replace'."));
		}
		break;

	case ZPOOL_STATUS_REMOVED_DEV:
		if (!json->json && !json->ld_json) {
			(void) printf(gettext("status: One or more devices has "
			    "been removed by the administrator.\n\tSufficient "
			    "replicas exist for the pool to"
			    " continue functioning in "
			    "a\n\tdegraded state.\n"));
			(void) printf(gettext("action: Online the device using "
			    "'zpool online' or replace the"
			    " device with\n\t'zpool "
			    "replace'.\n"));
		} else {
			(void) fnvlist_add_string(
			    json->nv_dict_buff,
				"status", gettext("One or more devices has "
			    "been removed by the administrator. Sufficient "
			    "replicas exist for the pool to"
			    " continue functioning in "
			    "a degraded state."));
			(void) fnvlist_add_string(
			    json->nv_dict_buff,
			    "action", gettext("Online the device using "
			    "'zpool online' or replace the device with 'zpool "
			    "replace'."));
		}
		break;

	case ZPOOL_STATUS_RESILVERING:
		if (!json->json && !json->ld_json) {
			(void) printf(gettext("status: One or more devices is "
			    "currently being resilvered.  "
			    "The pool will\n\tcontinue "
			    "to function, possibly in a degraded state.\n"));
			(void) printf(gettext("action: Wait for "
			    "the resilver to complete.\n"));
			} else {
			(void) fnvlist_add_string(
			    json->nv_dict_buff,
			    "status", gettext("One or more devices is "
			    "currently being resilvered. "
			    "The pool will continue "
			    "to function, possibly in a degraded state."));
			(void) fnvlist_add_string(
			    json->nv_dict_buff,
			    "action", gettext("Wait for the resilver to "
			    "complete."));
		}
		break;

	case ZPOOL_STATUS_CORRUPT_DATA:
		if (!json->json && !json->ld_json) {
			(void) printf(gettext("status: One or more devices has "
			    "experienced an error resulting"
			    " in data\n\tcorruption.  "
			    "Applications may be affected.\n"));
			(void) printf(gettext("action:"
				" Restore the file in question "
			    "if possible.  Otherwise restore "
			    "the\n\tentire pool from "
			    "backup.\n"));
		} else {
			(void) fnvlist_add_string(
			    json->nv_dict_buff,
			    "status", gettext("One or more devices has "
			    "experienced an error"
			    " resulting in data corruption.  "
			    "Applications may be affected."));
			(void) fnvlist_add_string(json->nv_dict_buff,
			    "action", gettext("Restore the file in question "
			    "if possible.  Otherwise restore"
			    " the entire pool from "
			    "backup."));
		}
		break;

	case ZPOOL_STATUS_CORRUPT_POOL:
		if (!json->json && !json->ld_json)
			(void) printf(gettext("status: "
				"The pool metadata is corrupted "
			    "and the pool cannot be opened.\n"));
		else
			fnvlist_add_string(
			    json->nv_dict_buff,
			    "status", gettext("The pool metadata is corrupted "
			    "and the pool cannot be opened."));
		zpool_explain_recover(zpool_get_handle(zhp),
		    zpool_get_name(zhp), reason, config, json);
		break;

	case ZPOOL_STATUS_VERSION_OLDER:
		if (!json->json && !json->ld_json) {
			(void) printf(gettext("status: The pool is "
			    "formatted using a "
			    "legacy on-disk format.  "
			    "The pool can\n\tstill be used, "
			    "but some features are unavailable.\n"));
			(void) printf(gettext("action: Upgrade the "
				"pool using 'zpool "
			    "upgrade'.  Once this is done, "
			    "the\n\tpool will no longer "
			    "be accessible on software that "
			    "does not support\n\t"
			    "feature flags.\n"));
		} else {
			(void) fnvlist_add_string(
			    json->nv_dict_buff,
			    "status", gettext("The pool is formatted using a "
			    "legacy on-disk format. "
			    "The pool can still be used, "
			    "but some features are unavailable."));
			(void) fnvlist_add_string(json->nv_dict_buff,
			    "action", gettext("Upgrade the pool using 'zpool "
			    "upgrade'.  Once this is done, "
			    "the pool will no longer "
			    "be accessible on software that does not support "
			    "feature flags."));
		}
		break;

	case ZPOOL_STATUS_VERSION_NEWER:
		if (!json->json && !json->ld_json) {
			(void) printf(gettext("status: The pool has "
			    "been upgraded to a "
			    "newer, incompatible on-disk"
			    " version.\n\tThe pool cannot "
			    "be accessed on this system.\n"));
			(void) printf(gettext("action: Access the "
				"pool from a system "
			    "running more recent software, "
			    "or\n\trestore the pool from backup.\n"));
		} else {
			(void) fnvlist_add_string(
			    json->nv_dict_buff,
			    "status", gettext("The pool has been upgraded to a "
			    "newer, incompatible on-disk version. "
			    "The pool cannot "
			    "be accessed on this system."));
			(void) fnvlist_add_string(json->nv_dict_buff,
			    "action", gettext("Access the pool from a system "
			    "running more recent software, or restore "
			    "the pool from backup."));
		}
		break;

	case ZPOOL_STATUS_FEAT_DISABLED:
		if (!json->json && !json->ld_json) {
			(void) printf(gettext("status: Some "
			    "supported features are not "
			    "enabled on the pool. "
			    "The pool can\n\tstill be used, but "
			    "some features are unavailable.\n"));
			(void) printf(gettext("action: Enable all "
			    "features using "
			    "'zpool upgrade'. Once this is "
			    "done,\n\tthe pool may no "
			    "longer be accessible by software "
			    "that does not support\n\t"
			    "the features. See zpool-features(5) "
			    "for details.\n"));
		} else {
			(void) fnvlist_add_string(
			    json->nv_dict_buff,
			    "status", gettext("Some supported features are not "
			    "enabled on the pool. The pool "
			    "can still be used, but "
			    "some features are unavailable."));
			(void) fnvlist_add_string(json->nv_dict_buff,
			    "action", gettext("Enable all features using "
			    "'zpool upgrade'. Once this is done, "
			    "the pool may no "
			    "longer be accessible by software that does "
			    "not support the features. See zpool-features(5) "
			    "for details."));
		}
		break;

	case ZPOOL_STATUS_UNSUP_FEAT_READ:
		if (!json->json && !json->ld_json)
			(void) printf(gettext("status: The pool "
			    "cannot be accessed on "
			    "this system because it uses "
			    "the\n\tfollowing feature(s) "
			    "not supported on this system:\n"));
		else
			json->json_buffer = strdup(
			    gettext("The pool cannot be accessed on "
			    "this system because it uses "
			    "the following feature(s) "
			    "not supported on this system: "));
		zpool_print_unsup_feat(config, json, json->json_buffer);
		if (!json->json && !json->ld_json) {
			(void) printf("\n");
			(void) printf(gettext("action: Access "
			    "the pool from a system "
			    "that supports the required "
			    "feature(s),\n\tor restore the "
			    "pool from backup.\n"));
		} else {
			(void) fnvlist_add_string(json->nv_dict_buff,
			    "action", gettext("Access the pool from a system "
			    "that supports the required feature(s), "
			    "or restore the pool from backup."));
			free(json->json_buffer);
		}
		break;

	case ZPOOL_STATUS_UNSUP_FEAT_WRITE:
		if (!json->json && !json->ld_json)
			(void) printf(gettext("status: The pool can "
			    "only be accessed "
			    "in read-only mode on this system. It\n\tcannot be "
			    "accessed in read-write mode because it uses the "
			    "following\n\tfeature(s) not supported "
			    "on this system:\n"));
		else
			json->json_buffer = strdup(
			    gettext("The pool can only be accessed "
			    "in read-only mode on this system. It cannot be "
			    "accessed in read-write mode because it uses the "
			    "following feature(s) not supported "
			    "on this system: "));
		zpool_print_unsup_feat(config, json, json->json_buffer);
		if (!json->json && !json->ld_json) {
			(void) printf("\n");
			(void) printf(gettext("action: The pool "
			    "cannot be accessed in "
			    "read-write mode. Import the pool with\n"
			    "\t\"-o readonly=on\", access the pool "
			    "from a system that "
			    "supports the\n\trequired feature(s), "
			    "or restore the pool from backup.\n"));
		} else {
			(void) fnvlist_add_string(json->nv_dict_buff,
			    "action", gettext("The pool cannot be accessed in "
			    "read-write mode. Import the pool with "
			    "\"-o readonly=on\", access the "
			    "pool from a system that "
			    "supports the required feature(s), or restore the "
			    "pool from backup."));
			free(json->json_buffer);
		}
		break;

	case ZPOOL_STATUS_FAULTED_DEV_R:
		if (!json->json && !json->ld_json) {
			(void) printf(gettext("status: One or more devices are "
			    "faulted in response to persistent "
			    "errors.\n\tSufficient "
			    "replicas exist for the pool "
			    "to continue functioning "
			    "in a\n\tdegraded state.\n"));
			(void) printf(gettext("action: Replace the faulted "
			    "device, or use 'zpool clear' to mark the "
			    "device\n\trepaired.\n"));
		} else {
			(void) fnvlist_add_string(
			    json->nv_dict_buff,
			    "status", gettext("One or more devices are "
			    "faulted in response to "
			    "persistent errors. Sufficient "
			    "replicas exist for the pool "
			    "to continue functioning "
			    "in a degraded state."));
			(void) fnvlist_add_string(json->nv_dict_buff,
			    "action", gettext("Replace the faulted device, "
			    "or use 'zpool clear' to mark "
			    "the device repaired."));
		}
		break;

	case ZPOOL_STATUS_FAULTED_DEV_NR:
		if (!json->json && !json->ld_json) {
			(void) printf(gettext("status: One or more devices are "
			    "faulted in response to "
			    "persistent errors.  There are "
			    "insufficient replicas for the pool to\n\tcontinue "
			    "functioning.\n"));
			(void) printf(gettext("action: Destroy and "
			    "re-create the pool "
			    "from a backup source.  Manually "
			    "marking the device\n"
			    "\trepaired using 'zpool clear' may allow "
			    "some data to be recovered.\n"));
		} else {
			(void) fnvlist_add_string(
			    json->nv_dict_buff,
			    "status", gettext("One or more devices are "
			    "faulted in response to "
			    "persistent errors. There are "
			    "insufficient replicas for the pool to continue "
			    "functioning."));
			(void) fnvlist_add_string(json->nv_dict_buff,
			    "action", gettext("Destroy and re-create the pool "
			    "from a backup source. Manually marking the device "
			    "repaired using 'zpool clear' may allow some data "
			    "to be recovered."));
		}
		break;

	case ZPOOL_STATUS_IO_FAILURE_WAIT:
	case ZPOOL_STATUS_IO_FAILURE_CONTINUE:
		if (!json->json && !json->ld_json) {
			(void) printf(gettext("status: One or more devices are "
			    "faulted in response to IO failures.\n"));
			(void) printf(gettext("action: Make "
			    "sure the affected devices "
			    "are connected, then run 'zpool clear'.\n"));
		} else {
			(void) fnvlist_add_string(
			    json->nv_dict_buff,
			    "status", gettext("One or more devices are "
			    "faulted in response to IO failures."));
			(void) fnvlist_add_string(json->nv_dict_buff,
			    "action", gettext("Make sure the affected devices "
			    "are connected, then run 'zpool clear'."));
		}
		break;

	case ZPOOL_STATUS_BAD_LOG:
		if (!json->json && !json->ld_json) {
			(void) printf(gettext("status: An intent log record "
			    "could not be read.\n"
			    "\tWaiting for adminstrator "
			    "intervention to fix the "
			    "faulted pool.\n"));
			(void) printf(gettext("action: Either "
			    "restore the affected "
			    "device(s) and run 'zpool online',\n"
			    "\tor ignore the intent log records by running "
			    "'zpool clear'.\n"));
		} else {
			(void) fnvlist_add_string(
			    json->nv_dict_buff,
			    "status", gettext("An intent log record "
			    "could not be read. "
			    "Waiting for adminstrator intervention to fix the "
			    "faulted pool."));
			(void) fnvlist_add_string(json->nv_dict_buff,
			    "action", gettext("Either restore the affected "
			    "device(s) and run 'zpool online', "
			    "or ignore the intent log records by running "
			    "'zpool clear'."));
		}
		break;

	case ZPOOL_STATUS_HOSTID_MISMATCH:
		if (!json->json && !json->ld_json) {
			(void) printf(gettext("status: Mismatch "
			    "between pool hostid "
			    "and system hostid on imported "
			    "pool.\n\tThis pool was "
			    "previously imported into a "
			    "system with a different "
			    "hostid,\n\tand then was verbatim "
			    "imported into this system.\n"));
			(void) printf(gettext("action: Export this "
			    "pool on all systems "
			    "on which it is imported.\n"
			    "\tThen import it to correct the mismatch.\n"));
		} else {
			(void) fnvlist_add_string(
			    json->nv_dict_buff,
			    "status", gettext("Mismatch between pool hostid "
			    "and system hostid on imported pool. This pool was "
			    "previously imported into a system with a "
			    "different hostid, and then was verbatim imported "
			    "into this system."));
			(void) fnvlist_add_string(json->nv_dict_buff,
			    "action", gettext("Export this pool on all systems "
			    "on which it is imported. "
			    "Then import it to correct the mismatch."));
		}
		break;

	case ZPOOL_STATUS_ERRATA:
		(void) snprintf(errbuf, sizeof (errbuf),
		    gettext("Errata #%d detected."),
		    errata);
		if (!json->json && !json->ld_json)
			(void) printf("status: %s\n", errbuf);
		else
			fnvlist_add_string(
			    json->nv_dict_buff,
			    "status", errbuf);

		switch (errata) {
		case ZPOOL_ERRATA_NONE:
			break;

		case ZPOOL_ERRATA_ZOL_2094_SCRUB:
			if (!json->json && !json->ld_json)
				(void) printf(gettext("action: To correct "
				    "the issue run 'zpool scrub'.\n"));
			else
				fnvlist_add_string(json->nv_dict_buff,
				    "action", gettext("To correct the issue "
				    "run 'zpool scrub'."));
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
	if (msgid != NULL) {
		(void) snprintf(errbuf, sizeof (errbuf),
		    gettext("http://zfsonlinux.org/msg/%s"),
		    msgid);
		if (!json->json && !json->ld_json)
			(void) printf(gettext("   see: %s\n"),
			    errbuf);
		else
			fnvlist_add_string(json->nv_dict_buff,
			    "see", errbuf);
	}

	if (config != NULL) {
		int namewidth;
		uint64_t nerr;
		nvlist_t **spares, **l2cache;
		uint_t nspares, nl2cache;
		pool_scan_stat_t *ps = NULL;
		(void) nvlist_lookup_uint64_array(nvroot,
		    ZPOOL_CONFIG_SCAN_STATS, (uint64_t **)&ps, &c);
		print_scan_status(ps, json);

		namewidth = max_width(zhp, nvroot, 0, 0, cbp->cb_name_flags);
		if (namewidth < 10)
			namewidth = 10;

		if (!json->json && !json->ld_json) {
			(void) printf(gettext("config:\n\n"));
			(void) printf(gettext("\t%-*s  %-8s %5s %5s %5s\n"),
			    namewidth, "NAME", "STATE",
			    "READ", "WRITE", "CKSUM");
		}
		if (json && (json->json || json->ld_json)) {
			print_status_config(zhp, zpool_get_name(zhp), nvroot,
			    namewidth, 0, B_FALSE, cbp->cb_name_flags, json,
			    json->nv_dict_buff_cpy);
			fnvlist_add_nvlist(json->nv_dict_buff, "config",
			    json->nv_dict_buff_cpy);
			fnvlist_free(json->nv_dict_buff_cpy);
		} else
			print_status_config(zhp, zpool_get_name(zhp), nvroot,
			    namewidth, 0, B_FALSE, cbp->cb_name_flags,
			    NULL, NULL);

		if (num_logs(nvroot) > 0)
			print_logs(zhp, nvroot, namewidth, B_TRUE,
			    cbp->cb_name_flags, NULL, NULL);
		if (nvlist_lookup_nvlist_array(nvroot, ZPOOL_CONFIG_L2CACHE,
		    &l2cache, &nl2cache) == 0)
			print_l2cache(zhp, l2cache, nl2cache, namewidth,
				cbp->cb_name_flags);

		if (nvlist_lookup_nvlist_array(nvroot, ZPOOL_CONFIG_SPARES,
		    &spares, &nspares) == 0)
			print_spares(zhp, spares, nspares, namewidth,
			    cbp->cb_name_flags);
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
			if (!json->json &&!json->ld_json)
				(void) printf("\n");

			if (nerr == 0) {
				(void) snprintf(errbuf, sizeof (errbuf),
				    gettext("No known data errors"));
				if (json->json || json->ld_json) {
					fnvlist_add_string(json->nv_dict_buff,
					    "errors", errbuf);
				} else
					printf("errors: %s\n", errbuf);
			} else if (!cbp->cb_verbose) {
				if (json->json || json->ld_json)
					(void) printf(gettext("errors: %llu "
					    "data errors, use '-v' for "
					    "a list\n"),
					    (u_longlong_t)nerr);
				else {
					(void) snprintf(errbuf, sizeof (errbuf),
					    gettext("errors: %llu data "
					    "errors, use '-v' for a list\n"),
					    (u_longlong_t)nerr);
					fnvlist_add_string(json->nv_dict_buff,
					    "errors", errbuf);
				}
			}
			else
				print_error_log(zhp, json);
		}
		if (cbp->cb_dedup_stats)
			print_dedup_stats(config, json);
	} else {
		if (json->json || json->ld_json)
			(void) printf(gettext("config: The configuration "
			    "cannot be determined.\n"));
		else
			fnvlist_add_string(json->nv_dict_buff,
			    "config", gettext("The configuration cannot be "
			    "determined."));
	}
	if (json->json) {
		json->nb_elem++;
		json->json_data = realloc(json->json_data,
	    sizeof (nvlist_t *) * json->nb_elem);
		    ((nvlist_t **)json->json_data)
		    [json->nb_elem - 1] =
		    json->nv_dict_buff;
	} else if (json->ld_json) {
		nvlist_print_json(stdout,
		    json->nv_dict_buff);
		fnvlist_free(json->nv_dict_buff);
		printf("\n");
		fflush(stdout);
	}

	return (0);
}


/*
 * zpool status [-gLPvx] [-T d|u] [pool] ... [interval [count]]
 *
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
	unsigned long interval = 0, count = 0;
	status_cbdata_t cb = { 0 };
	zfs_json_t json;
	json.json = json.ld_json = json.timestamp = B_FALSE;
	char errbuf[1024];
	char *buff;

	/* check options */
	while ((c = getopt(argc, argv, "JjgLPvxDT:")) != -1) {
		switch (c) {
		case 'j':
			if (json.json || json.ld_json)
				break;
			json.ld_json = B_TRUE;
			json.nv_dict_error = fnvlist_alloc();
			json.nv_dict_props = fnvlist_alloc();
			fnvlist_add_string(json.nv_dict_error, "error", "");
			break;
		case 'J':
			if (json.json)
				break;
			json.json = B_TRUE;
			if (!json.ld_json) {
				json.nv_dict_props = fnvlist_alloc();
				json.nv_dict_error = fnvlist_alloc();
			} else
				json.ld_json = B_FALSE;
			json.nb_elem = 0;
			json.json_data = NULL;
			fnvlist_add_string(json.nv_dict_props,
			    "cmd", "zpool status");
			fnvlist_add_string(json.nv_dict_error,
			    "error", "");
			fnvlist_add_string(json.nv_dict_props,
			    "schema_version", "1.0");
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
			(void) snprintf(errbuf, sizeof (errbuf),
			    gettext("invalid option '%c'"),
			    optopt);
			if (!json.json && !json.ld_json)
				fprintf(stderr, "%s\n", errbuf);
			else
				fnvlist_add_string(json.nv_dict_error,
				    "error", errbuf);
			goto usage;
		}
	}

	argc -= optind;
	argv += optind;
	get_interval_count(&argc, argv, &interval, &count);
	if (argc == 0)
		cb.cb_allpools = B_TRUE;
	cb.cb_first = B_TRUE;

	for (;;) {
		if (timestamp_fmt != NODATE) {
			if (!json.json && !json.ld_json)
				print_timestamp(timestamp_fmt);
			else {
				json.timestamp = B_TRUE;
				buff = get_timestamp(timestamp_fmt);
				fnvlist_add_string(json.nv_dict_props,
				    "date", buff);
				free(buff);
			}
		}
		ret = for_each_pool(argc, argv, B_TRUE, NULL,
		    status_callback, &cb, &json);
		if (argc == 0 && cb.cb_count == 0) {
			(void) snprintf(errbuf, sizeof (errbuf),
			    gettext("no pools available"));
			if (!json.json && !json.ld_json)
				fprintf(stderr, "%s\n", errbuf);
			else
				fnvlist_add_string(json.nv_dict_error,
				    "error", errbuf);
		} else if (cb.cb_explain && cb.cb_first && cb.cb_allpools) {
			(void) snprintf(errbuf, sizeof (errbuf),
			    gettext("all pools are healthy"));
			if (!json.json && !json.ld_json)
				fprintf(stdout, "%s\n", errbuf);
			else
				fnvlist_add_string(json.nv_dict_error,
				    "error", errbuf);
		}

		if (ret != 0) {
			if (json.json || json.ld_json)
				goto out;
			else
			    return (ret);
		}

		if (interval == 0)
			break;

		if (count != 0 && --count == 0)
			break;

		(void) sleep(interval);
	}
out:
	if (json.json || json.ld_json) {
		if (json.json) {
			fnvlist_add_nvlist_array(json.nv_dict_props,
			    "stdout", json.json_data,
			    json.nb_elem);
			fnvlist_add_nvlist(json.nv_dict_props,
			    "stderr", json.nv_dict_error);
			nvlist_print_json(stdout, json.nv_dict_props);
			while (((json.nb_elem)--) > 0)
				fnvlist_free(((nvlist_t **)(json.json_data))
				    [json.nb_elem]);
			free(json.json_data);
		} else
			nvlist_print_json(stdout, json.nv_dict_error);
		fprintf(stdout, "\n");
		fflush(stdout);
		fnvlist_free(json.nv_dict_props);
		fnvlist_free(json.nv_dict_error);
	}
	return (ret);
	usage :
		if (!json.json && !json.ld_json)
			usage(B_FALSE);
		else {
			if (!json.json)
				nvlist_print_json(stdout, json.nv_dict_error);
			else {
				fnvlist_add_string(json.nv_dict_props,
				    "schema_version", "1.0");
				fnvlist_add_nvlist_array(json.nv_dict_props,
				    "stdout", (nvlist_t **)json.json_data,
				    json.nb_elem);
				fnvlist_add_nvlist(json.nv_dict_props,
				    "stderr", json.nv_dict_error);
				nvlist_print_json(stdout, json.nv_dict_props);
			}
			fprintf(stdout, "\n");
			fflush(stdout);
			fnvlist_free(json.nv_dict_props);
			fnvlist_free(json.nv_dict_error);
		}
	exit(2);
}


typedef struct upgrade_cbdata {
	int	cb_first;
	int	cb_argc;
	uint64_t cb_version;
	char	**cb_argv;
} upgrade_cbdata_t;

static int
check_unsupp_fs(zfs_handle_t *zhp, void *unsupp_fs, zfs_json_t *json)
{
	int zfs_version = (int) zfs_prop_get_int(json, zhp, ZFS_PROP_VERSION);
	int *count = (int *)unsupp_fs;

	if (zfs_version > ZPL_VERSION) {
		(void) printf(gettext("%s (v%d) is not supported by this "
		    "implementation of ZFS.\n"),
		    zfs_get_name(zhp), zfs_version);
		(*count)++;
	}

	zfs_iter_filesystems(zhp, check_unsupp_fs, unsupp_fs, json);

	zfs_close(zhp);

	return (0);
}

static int
upgrade_version(zpool_handle_t *zhp, uint64_t version, zfs_json_t *json)
{
	int ret;
	nvlist_t *config;
	uint64_t oldversion;
	int unsupp_fs = 0;
	char errbuf[1024];

	config = zpool_get_config(zhp, NULL);
	verify(nvlist_lookup_uint64(config, ZPOOL_CONFIG_VERSION,
	    &oldversion) == 0);

	assert(SPA_VERSION_IS_SUPPORTED(oldversion));
	assert(oldversion < version);
	ret = zfs_iter_root(zpool_get_handle(zhp),
	    check_unsupp_fs, &unsupp_fs, json);
	if (ret != 0)
		return (ret);

	if (unsupp_fs) {
		(void) snprintf(errbuf, sizeof (errbuf),
		    gettext("Upgrade not performed due "
		    "to %d unsupported filesystems (max v%d).\n"),
		    unsupp_fs, (int) ZPL_VERSION);
		if (!json->json)
			fprintf(stderr, "%s\n", errbuf);
		else
			fnvlist_add_string(json->nv_dict_error,
			    "error", errbuf);
		return (1);
	}

	ret = zpool_upgrade(zhp, version, json);
	if (ret != 0)
		return (ret);

	if (version >= SPA_VERSION_FEATURES) {
		(void) printf(gettext("Successfully upgraded "
		    "'%s' from version %llu to feature flags.\n"),
		    zpool_get_name(zhp), (u_longlong_t) oldversion);
	} else {
		(void) printf(gettext("Successfully upgraded "
		    "'%s' from version %llu to version %llu.\n"),
		    zpool_get_name(zhp), (u_longlong_t) oldversion,
		    (u_longlong_t) version);
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
			    ZFS_FEATURE_ENABLED, NULL);
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
upgrade_cb(zpool_handle_t *zhp, void *arg, zfs_json_t *json)
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
		ret = upgrade_version(zhp, cbp->cb_version, json);
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
upgrade_list_older_cb(zpool_handle_t *zhp, void *arg, zfs_json_t *json)
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
upgrade_list_disabled_cb(zpool_handle_t *zhp, void *arg, zfs_json_t *json)
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
upgrade_one(zpool_handle_t *zhp, void *data, zfs_json_t *json)
{
	boolean_t printnl = B_FALSE;
	upgrade_cbdata_t *cbp = data;
	uint64_t cur_version;
	int ret;
	char errbuf[1024];

	if (strcmp("log", zpool_get_name(zhp)) == 0) {
		(void) snprintf(errbuf, sizeof (errbuf),
		    gettext("'log' is now a reserved word\n"
		    "Pool 'log' must be renamed using export and import"
		    " to upgrade.\n"));
		if (!json->json)
			fprintf(stderr, "%s\n", errbuf);
		else
			fnvlist_add_string(json->nv_dict_error,
			    "error", errbuf);
		return (1);
	}

	cur_version = zpool_get_prop_int(zhp, ZPOOL_PROP_VERSION, NULL);
	if (cur_version > cbp->cb_version) {
		(void) snprintf(errbuf, sizeof (errbuf),
		    gettext("Pool '%s' is already formatted "
		    "using more current version '%llu'."),
		    zpool_get_name(zhp), (u_longlong_t) cur_version);
		if (!json->json)
			printf("%s\n\n", errbuf);
		else
			fnvlist_add_string(json->nv_dict_error,
			    "error", errbuf);
		return (0);
	}

	if (cbp->cb_version != SPA_VERSION && cur_version == cbp->cb_version) {
		(void) snprintf(errbuf, sizeof (errbuf),
		    gettext("Pool '%s' is already formatted "
		    "using version %llu."), zpool_get_name(zhp),
		    (u_longlong_t) cbp->cb_version);
		if (!json->json)
			printf("%s\n\n", errbuf);
		else
			fnvlist_add_string(json->nv_dict_error,
			    "error", errbuf);
		return (0);
	}

	if (cur_version != cbp->cb_version) {
		printnl = B_TRUE;
		ret = upgrade_version(zhp, cbp->cb_version, json);
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
			(void) snprintf(errbuf, sizeof (errbuf),
			    gettext("Pool '%s' already has all "
			    "supported features enabled."),
			    zpool_get_name(zhp));
			if (!json->json)
				printf("%s\n", errbuf);
			else
				fnvlist_add_string(json->nv_dict_error,
				    "error", errbuf);
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
	zfs_json_t json;
	json.json = json.ld_json = B_FALSE;
	char errbuf[1024];

	/* check options */
	while ((c = getopt(argc, argv, ":jJavV:")) != -1) {
		switch (c) {
		case 'j':
		case 'J':
			if (json.json)
				break;
			if (showversions)
				showversions = B_FALSE;
			json.json = B_TRUE;
			json.nv_dict_props = fnvlist_alloc();
			json.nv_dict_error = fnvlist_alloc();
			json.nb_elem = 0;
			json.json_data = NULL;
			fnvlist_add_string(json.nv_dict_props,
			    "cmd", "zpool upgrade");
			fnvlist_add_string(json.nv_dict_error, "error", "");
			break;
		case 'a':
			upgradeall = B_TRUE;
			break;
		case 'v':
			if (!json.json)
				showversions = B_TRUE;
			break;
		case 'V':
			cb.cb_version = strtoll(optarg, &end, 10);
			if (*end != '\0' ||
			    !SPA_VERSION_IS_SUPPORTED(cb.cb_version)) {
				(void) snprintf(errbuf, sizeof (errbuf),
				    gettext("invalid version '%s'"), optarg);
				if (!json.json)
					fprintf(stderr, "%s\n", errbuf);
				else
					fnvlist_add_string(json.nv_dict_error,
					    "error", errbuf);
				goto json_usage;
			}
			break;
		case ':':
			(void) snprintf(errbuf, sizeof (errbuf),
			    gettext("missing argument for "
			    "'%c' option"), optopt);
			if (!json.json)
				fprintf(stderr, "%s\n", errbuf);
			else
				fnvlist_add_string(json.nv_dict_error,
				    "error", errbuf);
			goto json_usage;
			break;
		case '?':
			(void) snprintf(errbuf, sizeof (errbuf),
			    gettext("invalid option '%c'"),
			    optopt);
			if (!json.json)
				fprintf(stderr, "%s\n", errbuf);
			else
				fnvlist_add_string(json.nv_dict_error,
				    "error", errbuf);
			goto json_usage;
		}
	}

	cb.cb_argc = argc;
	cb.cb_argv = argv;
	argc -= optind;
	argv += optind;

	if (cb.cb_version == 0) {
		cb.cb_version = SPA_VERSION;
	} else if (!upgradeall && argc == 0) {
		(void) snprintf(errbuf, sizeof (errbuf),
		    gettext("-V option is "
		    "incompatible with other arguments"));
		if (!json.json)
			fprintf(stderr, "%s\n", errbuf);
		else
			fnvlist_add_string(json.nv_dict_error,
			    "error", errbuf);
		goto json_usage;
	}

	if (showversions) {
		if (upgradeall || argc != 0) {
			(void) snprintf(errbuf, sizeof (errbuf),
			    gettext("-v option is "
			    "incompatible with other arguments"));
			if (!json.json)
				fprintf(stderr, "%s\n", errbuf);
			else
				fnvlist_add_string(json.nv_dict_error,
				    "error", errbuf);
			goto json_usage;
		}
	} else if (upgradeall) {
		if (argc != 0) {
			(void) snprintf(errbuf, sizeof (errbuf),
			    gettext("-a option should not "
			    "be used along with a pool name"));
			if (!json.json)
				fprintf(stderr, "%s\n", errbuf);
			else
				fnvlist_add_string(json.nv_dict_error,
				    "error", errbuf);
			goto json_usage;
		}
	}
	if (!json.json)
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
		ret = zpool_iter(g_zfs, upgrade_cb, &cb, &json);
		if (ret == 0 && cb.cb_first) {
			if ((cb.cb_version == SPA_VERSION) && !json.json) {
				(void) printf(gettext("All pools are already "
				    "formatted using feature flags.\n\n"));
				(void) printf(gettext("Every feature flags "
				    "pool already has all supported features "
				    "enabled.\n"));
			} else {
				if (!json.json) {
					(void) printf(gettext(
					    "All pools are already "
					    "formatted with"
					    " version %llu or higher.\n"),
					    (u_longlong_t) cb.cb_version);
				}
			}
		}
	} else if (argc == 0) {
		cb.cb_first = B_TRUE;
		ret = zpool_iter(g_zfs, upgrade_list_older_cb, &cb, &json);
		assert(ret == 0);

		if (cb.cb_first && !json.json) {
			(void) printf(gettext("All pools are formatted "
			    "using feature flags.\n\n"));
		} else {
			if (!json.json) {
				(void) printf(gettext("\nUse 'zpool"
				    " upgrade -v' "
				    "for a list of available"
				    " legacy versions.\n"));
			}
		}

		cb.cb_first = B_TRUE;
		ret = zpool_iter(g_zfs, upgrade_list_disabled_cb, &cb, &json);
		assert(ret == 0);

		if (cb.cb_first && !json.json) {
			(void) printf(gettext("Every feature flags pool has "
			    "all supported features enabled.\n"));
		} else {
			if (!json.json) {
				(void) printf(gettext("\n"));
			}
		}
	} else {
		ret = for_each_pool(argc, argv, B_FALSE, NULL,
		    upgrade_one, &cb, &json);
	}

	if (json.json) {
		fnvlist_add_string(json.nv_dict_props,
		    "schema_version", "1.0");
		fnvlist_add_nvlist_array(json.nv_dict_props, "stdout",
		    (nvlist_t **)json.json_data, json.nb_elem);
		fnvlist_add_nvlist(json.nv_dict_props,
		    "stderr", json.nv_dict_error);
		nvlist_print_json(stdout, json.nv_dict_props);
		fprintf(stdout, "\n");
		fflush(stdout);
		while (((json.nb_elem)--) > 0)
			fnvlist_free(
			    ((nvlist_t **)
			    (json.json_data))[json.nb_elem]);
		free(json.json_data);
		fnvlist_free(json.nv_dict_error);
		fnvlist_free(json.nv_dict_props);
	}
	return (ret);
json_usage:
	if (!json.json)
		usage(B_FALSE);
	else {
		fnvlist_add_string(json.nv_dict_props,
		    "schema_version", "1.0");
		fnvlist_add_nvlist_array(json.nv_dict_props, "stdout",
		    (nvlist_t **)json.json_data, json.nb_elem);
		fnvlist_add_nvlist(json.nv_dict_props,
		    "stderr", json.nv_dict_error);
		nvlist_print_json(stdout, json.nv_dict_props);
		fprintf(stdout, "\n");
		fflush(stdout);
		while (((json.nb_elem)--) > 0)
			fnvlist_free(
			    ((nvlist_t **)
			    (json.json_data))[json.nb_elem]);
		free(json.json_data);
		fnvlist_free(json.nv_dict_error);
		fnvlist_free(json.nv_dict_props);
	}
	exit(2);
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
get_history_one(zpool_handle_t *zhp, void *data, zfs_json_t *json)
{
	nvlist_t *nvhis;
	nvlist_t **records;
	uint_t numrecords;
	int ret, i;
	hist_cbdata_t *cb = (hist_cbdata_t *)data;
	int  nvcount = 0;
	char errbuf[1024];
	nvlist_t *buffer = NULL;
	nvlist_t **buffer2 = NULL;
	boolean_t is_alloc = B_FALSE;
	cb->first = B_FALSE;

	if (!json->json && !json->ld_json) {
		(void) printf(gettext("History "
		    "for '%s':\n"), zpool_get_name(zhp));
		if ((ret = zpool_get_history(zhp, &nvhis, json)) != 0)
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
				(void) strftime(tbuf,
				    sizeof (tbuf), "%F.%T", &t);
			}

			if (nvlist_exists(rec, ZPOOL_HIST_CMD)) {
				(void) printf("%s %s", tbuf,
				    fnvlist_lookup_string(rec, ZPOOL_HIST_CMD));
			} else if (nvlist_exists(rec, ZPOOL_HIST_INT_EVENT)) {
				int ievent =
				    fnvlist_lookup_uint64(rec,
				    ZPOOL_HIST_INT_EVENT);
				if (!cb->internal)
					continue;
				if (ievent >= ZFS_NUM_LEGACY_HISTORY_EVENTS) {
					(void) printf("%s unrecognized"
					    " record:\n", tbuf);
					dump_nvlist(rec, 4);
					continue;
				}
				(void) printf("%s [internal %s"
					" txg:%lld] %s", tbuf,
				    zfs_history_event_names[ievent],
				    (longlong_t) fnvlist_lookup_uint64(
				    rec, ZPOOL_HIST_TXG),
				    fnvlist_lookup_string(rec,
				    ZPOOL_HIST_INT_STR));
			} else if (nvlist_exists(rec, ZPOOL_HIST_INT_NAME)) {
				if (!cb->internal)
					continue;
				(void) printf("%s [txg:%lld] %s", tbuf,
				    (longlong_t) fnvlist_lookup_uint64(
				    rec, ZPOOL_HIST_TXG),
				    fnvlist_lookup_string(rec,
				    ZPOOL_HIST_INT_NAME));
				if (nvlist_exists(rec, ZPOOL_HIST_DSNAME)) {
					(void) printf(" %s (%llu)",
					    fnvlist_lookup_string(rec,
					    ZPOOL_HIST_DSNAME),
					    (u_longlong_t)
					    fnvlist_lookup_uint64(rec,
					    ZPOOL_HIST_DSID));
				}
				(void) printf(" %s", fnvlist_lookup_string(rec,
				    ZPOOL_HIST_INT_STR));
			} else if (nvlist_exists(rec, ZPOOL_HIST_IOCTL)) {
				if (!cb->internal)
					continue;
				(void) printf("%s ioctl %s\n", tbuf,
				    fnvlist_lookup_string(rec,
				    ZPOOL_HIST_IOCTL));
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
				(void) printf("%s unrecognized"
				    " record:\n", tbuf);
				dump_nvlist(rec, 4);
			}

			if (!cb->longfmt) {
				(void) printf("\n");
				continue;
			}
			(void) printf(" [");
			if (nvlist_exists(rec, ZPOOL_HIST_WHO)) {
				uid_t who = fnvlist_lookup_uint64(rec,
				    ZPOOL_HIST_WHO);
				struct passwd *pwd = getpwuid(who);
				(void) printf("user %d ", (int)who);
				if (pwd != NULL)
					(void) printf("(%s) ", pwd->pw_name);
			}
			if (nvlist_exists(rec, ZPOOL_HIST_HOST)) {
				(void) printf("on %s",
				    fnvlist_lookup_string(rec,
				    ZPOOL_HIST_HOST));
			}
			if (nvlist_exists(rec, ZPOOL_HIST_ZONE)) {
				(void) printf(":%s",
				    fnvlist_lookup_string(rec,
				    ZPOOL_HIST_ZONE));
			}

			(void) printf("]");
			(void) printf("\n");
		}
		(void) printf("\n");
		nvlist_free(nvhis);
	} else {
		if ((ret = zpool_get_history(zhp, &nvhis, json)) != 0)
			return (ret);
		buffer = fnvlist_alloc();
		buffer2 = NULL;
		fnvlist_add_string(buffer,
		    "name", zpool_get_name(zhp));
		verify(nvlist_lookup_nvlist_array(nvhis, ZPOOL_HIST_RECORD,
		    &records, &numrecords) == 0);
		for (i = 0; i < numrecords; i++) {
			nvlist_t *rec = records[i];
			char tbuf[30] = "";
			is_alloc = B_FALSE;
			if (nvlist_exists(rec, ZPOOL_HIST_TIME)) {
				time_t tsec;
				struct tm t;

				tsec = fnvlist_lookup_uint64(records[i],
				    ZPOOL_HIST_TIME);
				(void) localtime_r(&tsec, &t);
				(void) strftime(tbuf,
				    sizeof (tbuf), "%F.%T", &t);
			}
			if (nvlist_exists(rec, ZPOOL_HIST_CMD)) {
				nvcount ++;
				buffer2 = realloc(buffer2,
				sizeof (nvlist_t *) * nvcount -1);
				buffer2[nvcount -1 ] = fnvlist_alloc();
				is_alloc = B_TRUE;
				fnvlist_add_string(buffer2[nvcount - 1],
				    "command", fnvlist_lookup_string(rec,
				    ZPOOL_HIST_CMD));
				fnvlist_add_string(buffer2[nvcount - 1],
				    "date", tbuf);
			} else if (nvlist_exists(rec, ZPOOL_HIST_INT_EVENT)) {
				int ievent =
				    fnvlist_lookup_uint64(rec,
				    ZPOOL_HIST_INT_EVENT);
				if (!cb->internal)
					continue;
				if (ievent >= ZFS_NUM_LEGACY_HISTORY_EVENTS) {
					fnvlist_add_string(buffer2[nvcount - 1],
					    "date", tbuf);
					fnvlist_add_nvlist(buffer2[nvcount - 1],
					    "unrecognized record", rec);
					continue;
				}
				fnvlist_add_string(buffer2[nvcount - 1],
				    "date", tbuf);
				fnvlist_add_string(buffer2[nvcount - 1],
				    "internal",
				    zfs_history_event_names[ievent]);
				snprintf(errbuf, sizeof (errbuf),
				    "%lld", (longlong_t)
				    fnvlist_lookup_uint64(
				    rec, ZPOOL_HIST_TXG));
				fnvlist_add_string(buffer2[nvcount - 1],
				    "tgx", errbuf);
				fnvlist_add_string(buffer2[nvcount - 1],
				    "dont know",  fnvlist_lookup_string(rec,
				    ZPOOL_HIST_INT_STR));
				nvlist_print_json(stdout, buffer2[nvcount - 1]);
			} else if (nvlist_exists(rec, ZPOOL_HIST_INT_NAME)) {
				if (!cb->internal)
					continue;
				if (!is_alloc) {
					nvcount ++;
					buffer2 = realloc(buffer2,
					    sizeof (nvlist_t *) * nvcount -1);
					buffer2[nvcount -1 ] = fnvlist_alloc();
					is_alloc = B_TRUE;
				}
				fnvlist_add_string(buffer2[nvcount - 1],
				    "date", tbuf);
				fnvlist_add_string(buffer2[nvcount - 1],
				    "type", fnvlist_lookup_string(rec,
				    ZPOOL_HIST_INT_NAME));
				snprintf(errbuf, sizeof (errbuf),
				    "%lld", (longlong_t)
				    fnvlist_lookup_uint64(
				    rec, ZPOOL_HIST_TXG));
				fnvlist_add_string(buffer2[nvcount - 1],
				    "tgx", errbuf);
				if (nvlist_exists(rec, ZPOOL_HIST_DSNAME)) {
					fnvlist_add_string(buffer2[nvcount - 1],
					    "vol name",
					    fnvlist_lookup_string(rec,
					    ZPOOL_HIST_DSNAME));
						snprintf(errbuf,
						sizeof (errbuf), "%llu",
						(u_longlong_t)
						fnvlist_lookup_uint64(rec,
					    ZPOOL_HIST_DSID));
					fnvlist_add_string(buffer2[nvcount - 1],
					    "history id", errbuf);
				}
				fnvlist_add_string(buffer2[nvcount - 1],
				    "system info",
				    fnvlist_lookup_string(rec,
				    ZPOOL_HIST_INT_STR));
			} else if (nvlist_exists(rec, ZPOOL_HIST_IOCTL)) {
				if (!cb->internal)
					continue;
				fnvlist_add_string(buffer2[nvcount - 1],
				    "date", tbuf);
				fnvlist_add_string(buffer2[nvcount - 1],
				    "ioctl", fnvlist_lookup_string(rec,
				    ZPOOL_HIST_IOCTL));
				if (nvlist_exists(rec, ZPOOL_HIST_INPUT_NVL)) {
					fnvlist_add_nvlist(buffer2[nvcount - 1],
					"input", fnvlist_lookup_nvlist(rec,
					    ZPOOL_HIST_INPUT_NVL));
				}
				if (nvlist_exists(rec, ZPOOL_HIST_OUTPUT_NVL)) {
					fnvlist_add_nvlist(buffer2[nvcount - 1],
					    "output", fnvlist_lookup_nvlist(rec,
					    ZPOOL_HIST_OUTPUT_NVL));
				}
			} else {
				if (!cb->internal)
					continue;
				fnvlist_add_string(buffer2[nvcount-1],
				    "date", tbuf);
				fnvlist_add_nvlist(buffer2[nvcount-1],
				    "unrecognized record", rec);
			}
			if (!cb->longfmt)
				continue;
			if (nvlist_exists(rec, ZPOOL_HIST_WHO)) {
				uid_t who = fnvlist_lookup_uint64(rec,
				    ZPOOL_HIST_WHO);
				struct passwd *pwd = getpwuid(who);
				if (pwd != NULL)
					(void) snprintf(errbuf,
					    sizeof (errbuf), "%d (%s) ",
					    (int)who, pwd->pw_name);
				else
					snprintf(errbuf, sizeof (errbuf),
					    "%d", (int) who);
					fnvlist_add_string(buffer2[nvcount - 1],
					    "user", errbuf);
			}
			if (nvlist_exists(rec, ZPOOL_HIST_HOST)) {
				fnvlist_add_string(buffer2[nvcount - 1],
				    "on",
				    fnvlist_lookup_string(rec,
				    ZPOOL_HIST_HOST));
			}
		}
		nvlist_free(nvhis);
	}

	if (json->ld_json) {
		fnvlist_add_nvlist_array(buffer, "history",
		    buffer2, nvcount);
		nvlist_print_json(stdout, buffer);
		printf("\n");
		fflush(stdout);
		while (nvcount-- > 0)
			fnvlist_free(buffer2[nvcount]);
		free(buffer2);
		fnvlist_free(buffer);
	} else if (json->json) {
		fnvlist_add_nvlist_array(buffer, "history",
		    buffer2, nvcount);
			json->nb_elem++;
			json->json_data = realloc(json->json_data,
		    sizeof (nvlist_t *) * json->nb_elem);
		    ((nvlist_t **)json->json_data)[json->nb_elem - 1] =
		    buffer;
		while (nvcount-- > 0)
			fnvlist_free(buffer2[nvcount]);
		free(buffer2);
	}
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
	zfs_json_t json;
	json.json = json.ld_json = B_FALSE;
	char errbuf[1024];

	cbdata.first = B_TRUE;
	/* check options */
	while ((c = getopt(argc, argv, "Jjli")) != -1) {
		switch (c) {
		case 'j':
			if (json.json || json.ld_json)
				break;
			json.ld_json = B_TRUE;
			json.nv_dict_props = fnvlist_alloc();
			json.nv_dict_error = fnvlist_alloc();
			json.json_data = NULL;
			json.nb_elem = 0;
			fnvlist_add_string(json.nv_dict_error,
			    "error", "");
			break;
		case 'J':
			if (json.json)
				break;
			json.json = B_TRUE;
			if (!json.ld_json) {
				json.nv_dict_props = fnvlist_alloc();
				json.nv_dict_error = fnvlist_alloc();
			} else
				json.ld_json = B_FALSE;
			json.json_data = NULL;
			json.nb_elem = 0;
			fnvlist_add_string(json.nv_dict_error,
			    "error", "");
			fnvlist_add_string(json.nv_dict_props,
			    "cmd", "zpool history");
			break;
		case 'l':
			cbdata.longfmt = B_TRUE;
			break;
		case 'i':
			cbdata.internal = B_TRUE;
			break;
		case '?':
			(void) snprintf(errbuf, sizeof (errbuf),
			    gettext("invalid option '%c'"),
			    optopt);
			if (!json.json && !json.ld_json)
				fprintf(stderr, "%s\n", errbuf);
			else {
				fnvlist_add_string(json.nv_dict_error,
				    "error", errbuf);
				goto usage;
			}
			usage(B_FALSE);
		}
	}
	argc -= optind;
	argv += optind;

	ret = for_each_pool(argc, argv, B_FALSE,  NULL, get_history_one,
	    &cbdata, &json);

	if (argc == 0 && cbdata.first == B_TRUE) {
		(void) snprintf(errbuf, sizeof (errbuf),
		    gettext("no pools available"));
		if (!json.json && !json.ld_json) {
			fprintf(stderr, "%s\n", errbuf);
			return (0);
		} else
			fnvlist_add_string(json.nv_dict_error,
			    "error", errbuf);
			ret = 0;
	}
	if (json.json || json.ld_json) {
		if (json.json) {
			fnvlist_add_string(json.nv_dict_props,
			    "schema_version", "1.0");
			fnvlist_add_nvlist_array(json.nv_dict_props, "stdout",
			    (nvlist_t **)json.json_data, json.nb_elem);
			fnvlist_add_nvlist(json.nv_dict_props,
			    "stderr", json.nv_dict_error);
			nvlist_print_json(stdout, json.nv_dict_props);
			while (((json.nb_elem)--) > 0)
				fnvlist_free(
				    ((nvlist_t **)
				    (json.json_data))[json.nb_elem]);
			free(json.json_data);
		} else
			nvlist_print_json(stdout, json.nv_dict_error);
		fnvlist_free(json.nv_dict_props);
		fnvlist_free(json.nv_dict_error);
		fprintf(stdout, "\n");
		fflush(stdout);
	}
	return (ret);
usage:
	if (json.json || json.ld_json) {
		if (json.json) {
			fnvlist_add_string(json.nv_dict_props,
			    "schema_version", "1.0");
			fnvlist_add_nvlist_array(json.nv_dict_props, "stdout",
			    (nvlist_t **)json.json_data, json.nb_elem);
			fnvlist_add_nvlist(json.nv_dict_props,
			    "stderr", json.nv_dict_error);
			nvlist_print_json(stdout, json.nv_dict_props);
			while (((json.nb_elem)--) > 0)
				fnvlist_free(
				    ((nvlist_t **)
				    (json.json_data))[json.nb_elem]);
			free(json.json_data);
		} else
			nvlist_print_json(stdout, json.nv_dict_error);
		fnvlist_free(json.nv_dict_props);
		fnvlist_free(json.nv_dict_error);
		fprintf(stdout, "\n");
		fflush(stdout);
	}
	exit(2);
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
			printf(gettext("0x%llx"), (u_longlong_t)i64);
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
get_callback(zpool_handle_t *zhp, void *data, zfs_json_t *json)
{
	zprop_get_cbdata_t *cbp = (zprop_get_cbdata_t *)data;
	char value[MAXNAMELEN];
	zprop_source_t srctype;
	zprop_list_t *pl;
	nvlist_t *buffer_t;

	if (json->json) {
		json->nb_buff = 0;
		json->json_buff = NULL;
	}
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
				zprop_print_one_property(json,
				    zpool_get_name(zhp),
				    cbp, pl->pl_user_prop, value, srctype,
				    NULL, NULL);
			}
		} else {
			if (zpool_get_prop_literal(zhp, pl->pl_prop, value,
			    sizeof (value), &srctype, cbp->cb_literal) != 0)
				continue;
			zprop_print_one_property(json,
			    zpool_get_name(zhp), cbp,
			    zpool_prop_to_name(pl->pl_prop), value, srctype,
			    NULL, NULL);
		}
	}
	if (json->json) {
		buffer_t = fnvlist_alloc();
		fnvlist_add_string(buffer_t, "name", zpool_get_name(zhp));
		fnvlist_add_nvlist_array(buffer_t, "properties",
			    json->json_buff, json->nb_buff);
		while (((json->nb_buff)--) > 0)
				fnvlist_free(((nvlist_t **)json->json_buff)
				    [json->nb_buff]);
		free(json->json_buff);
		json->nb_elem++;
		json->json_data = realloc(json->json_data,
			    sizeof (nvlist_t *) * json->nb_elem);
		((nvlist_t **)json->json_data)[json->nb_elem - 1] =
			    buffer_t;
	}
	return (0);
}

int
zpool_do_get(int argc, char **argv)
{
	zprop_get_cbdata_t cb = { 0 };
	zprop_list_t fake_name = { 0 };
	int c, ret;
	zfs_json_t json;
	json.json = json.ld_json = B_FALSE;
	char errbuf[1024];

	/* check options */
	while ((c = getopt(argc, argv, "pHJj")) != -1) {
		switch (c) {
		case 'J':
			if (json.json)
				break;
			json.json = B_TRUE;
			if (!json.ld_json)
				json.nv_dict_error = fnvlist_alloc();
			else
				json.ld_json = B_FALSE;
			json.nv_dict_props = fnvlist_alloc();
			json.nb_elem = 0;
			json.json_data = NULL;
			cb.cb_literal = B_TRUE;
			fnvlist_add_string(json.nv_dict_props,
			    "cmd", "zfs get");
			fnvlist_add_string(json.nv_dict_error, "error", "");
			break;
		case 'j':
			if (json.json || json.ld_json)
				break;
			json.nv_dict_error = fnvlist_alloc();
			fnvlist_add_string(json.nv_dict_error, "error", "");
			json.ld_json = B_TRUE;
			cb.cb_literal = B_TRUE;
			break;
		case 'p':
			cb.cb_literal = B_TRUE;
			break;

		case 'H':
			cb.cb_scripted = B_TRUE;
			break;

		case '?':
			(void) snprintf(errbuf, sizeof (errbuf),
			    gettext("invalid option '%c'"),
			    optopt);
			if (!json.json && !json.ld_json)
				fprintf(stderr, "%s\n", errbuf);
			else
				fnvlist_add_string(json.nv_dict_error,
				    "error", errbuf);
			goto json_usage;
		}
	}

	argc -= optind;
	argv += optind;

	if (argc < 1) {
		(void) snprintf(errbuf, sizeof (errbuf),
		    gettext("missing property "
		    "argument"));
		if (!json.json && !json.ld_json)
			fprintf(stderr, "%s\n", errbuf);
		else
			fnvlist_add_string(json.nv_dict_error, "error", errbuf);
		goto json_usage;
	}

	cb.cb_first = B_TRUE;
	cb.cb_sources = ZPROP_SRC_ALL;
	cb.cb_columns[0] = GET_COL_NAME;
	cb.cb_columns[1] = GET_COL_PROPERTY;
	cb.cb_columns[2] = GET_COL_VALUE;
	cb.cb_columns[3] = GET_COL_SOURCE;
	cb.cb_type = ZFS_TYPE_POOL;

	if (zprop_get_list(&json, g_zfs, argv[0],
	    &cb.cb_proplist, ZFS_TYPE_POOL) != 0)
		goto json_usage;

	argc--;
	argv++;

	if (cb.cb_proplist != NULL) {
		fake_name.pl_prop = ZPOOL_PROP_NAME;
		fake_name.pl_width = strlen(gettext("NAME"));
		fake_name.pl_next = cb.cb_proplist;
		cb.cb_proplist = &fake_name;
	}

	ret = for_each_pool(argc, argv, B_TRUE, &cb.cb_proplist,
	    get_callback, &cb, &json);

	if (cb.cb_proplist == &fake_name)
		zprop_free_list(fake_name.pl_next);
	else
		zprop_free_list(cb.cb_proplist);

	if (json.json || json.ld_json) {
		if (json.json) {
			fnvlist_add_string(json.nv_dict_props,
			    "schema_version", "1.0");
			fnvlist_add_nvlist_array(json.nv_dict_props, "stdout",
			    json.json_data, json.nb_elem);
			fnvlist_add_nvlist(json.nv_dict_props,
			    "stderr", json.nv_dict_error);
			nvlist_print_json(stdout, json.nv_dict_props);
			while (((json.nb_elem)--) > 0)
				fnvlist_free(((nvlist_t **)
				    json.json_data)[json.nb_elem]);
			free(json.json_data);
			fnvlist_free(json.nv_dict_props);
		} else
			nvlist_print_json(stdout, json.nv_dict_error);
		fprintf(stdout, "\n");
		fflush(stdout);
		fnvlist_free(json.nv_dict_error);
	}
	return (ret);
json_usage:
	if (!json.json && !json.ld_json)
		usage(B_FALSE);
	else {
		if (json.json) {
			fnvlist_add_string(json.nv_dict_props,
			    "schema_version", "1.0");
			fnvlist_add_nvlist_array(json.nv_dict_props, "stdout",
			    json.json_data, json.nb_elem);
			fnvlist_add_nvlist(json.nv_dict_props,
			    "stderr", json.nv_dict_error);
			nvlist_print_json(stdout, json.nv_dict_props);
			while (((json.nb_elem)--) > 0)
				fnvlist_free(((nvlist_t **)
				    json.json_data)[json.nb_elem]);
			free(json.json_data);
			fnvlist_free(json.nv_dict_props);
		} else
			nvlist_print_json(stdout, json.nv_dict_error);
		fprintf(stdout, "\n");
		fflush(stdout);
		fnvlist_free(json.nv_dict_error);
	}
	exit(2);
}

typedef struct set_cbdata {
	char *cb_propname;
	char *cb_value;
	boolean_t cb_any_successful;
} set_cbdata_t;

int
set_callback(zpool_handle_t *zhp, void *data, zfs_json_t *json)
{
	int error;
	set_cbdata_t *cb = (set_cbdata_t *)data;

	error = zpool_set_prop(zhp, cb->cb_propname, cb->cb_value, json);

	if (!error)
		cb->cb_any_successful = B_TRUE;

	return (error);
}

int
zpool_do_set(int argc, char **argv)
{
	set_cbdata_t cb = { 0 };
	int error;
	zfs_json_t json;
	json.json = json.ld_json = B_FALSE;
	char errbuf[1024];
	int c;

	while ((c = getopt(argc, argv, "jJ")) != -1) {
		switch (c) {
		case 'j':
		case 'J':
			if (json.json)
				break;
			json.json = B_TRUE;
			json.nv_dict_props = fnvlist_alloc();
			json.nv_dict_error = fnvlist_alloc();
			json.nb_elem = 0;
			json.json_data = NULL;
			fnvlist_add_string(json.nv_dict_props,
			    "cmd", "zpool set");
			fnvlist_add_string(json.nv_dict_error,
			    "error", "");
			fnvlist_add_string(json.nv_dict_props,
			    "schema_version", "1.0");
			break;
		case '?':
			(void) sprintf(errbuf, gettext("invalid option '%c'"),
			    optopt);
			if (!json.json) {
				fprintf(stderr, "%s\n", errbuf);
				usage(B_FALSE);
			} else {
				fnvlist_add_string(json.nv_dict_error,
				    "error", errbuf);
				goto json_usage;
			}
		}
	}

	argc -= optind;
	argv += optind;

	if (argc < 1) {
		(void) sprintf(errbuf, gettext("missing property=value "
		    "argument"));
		if (!json.json) {
			fprintf(stderr, "%s\n", errbuf);
			usage(B_FALSE);
		} else {
			fnvlist_add_string(json.nv_dict_error,
			    "error", errbuf);
			goto json_usage;
		}
		usage(B_FALSE);
	}

	if (argc < 2) {
		(void) sprintf(errbuf, gettext("missing pool name"));
		if (!json.json) {
			fprintf(stderr, "%s\n", errbuf);
			usage(B_FALSE);
		} else {
			fnvlist_add_string(json.nv_dict_error,
			    "error", errbuf);
			goto json_usage;
		}
		usage(B_FALSE);
	}

	if (argc > 2) {
		(void) sprintf(errbuf, gettext("too many pool names"));
		if (!json.json) {
			fprintf(stderr, "%s\n", errbuf);
			usage(B_FALSE);
		} else {
			fnvlist_add_string(json.nv_dict_error,
			    "error", errbuf);
			goto json_usage;
		}
		usage(B_FALSE);
	}

	cb.cb_propname = argv[0];
	cb.cb_value = strchr(cb.cb_propname, '=');
	if (cb.cb_value == NULL) {
		(void) sprintf(errbuf, gettext("missing value in "
		    "property=value argument"));
		if (!json.json) {
			fprintf(stderr, "%s\n", errbuf);
			usage(B_FALSE);
		} else {
			fnvlist_add_string(json.nv_dict_error,
			    "error", errbuf);
			goto json_usage;
		}
		usage(B_FALSE);
	}

	*(cb.cb_value) = '\0';
	cb.cb_value++;

	argc--;
	argv++;

	error = for_each_pool(argc, argv, B_TRUE, NULL,
	    set_callback, &cb, &json);
	if (json.json) {
		fnvlist_add_nvlist_array(json.nv_dict_props, "stdout",
		    (nvlist_t **)json.json_data, json.nb_elem);
		fnvlist_add_nvlist(json.nv_dict_props,
		    "stderr", json.nv_dict_error);
		nvlist_print_json(stdout, json.nv_dict_props);
		fprintf(stdout, "\n");
		fflush(stdout);
		while (((json.nb_elem)--) > 0)
			fnvlist_free(
			    ((nvlist_t **)
			    (json.json_data))[json.nb_elem]);
		free(json.json_data);
		fnvlist_free(json.nv_dict_error);
		fnvlist_free(json.nv_dict_props);
	}

	return (error);
	json_usage:
	if (json.json) {
		fnvlist_add_nvlist_array(json.nv_dict_props, "stdout",
		    (nvlist_t **)json.json_data, json.nb_elem);
		fnvlist_add_nvlist(json.nv_dict_props,
		    "stderr", json.nv_dict_error);
		nvlist_print_json(stdout, json.nv_dict_props);
		fprintf(stdout, "\n");
		fflush(stdout);
		while (((json.nb_elem)--) > 0)
			fnvlist_free(
			    ((nvlist_t **)
			    (json.json_data))[json.nb_elem]);
		free(json.json_data);
		fnvlist_free(json.nv_dict_error);
		fnvlist_free(json.nv_dict_props);
	}
	exit(2);

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
	int i = 0;
	char *cmdname;

	(void) setlocale(LC_ALL, "");
	(void) textdomain(TEXT_DOMAIN);

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
		ret = 1;
	}

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
