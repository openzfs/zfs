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
 * Copyright (c) 2011, 2020 by Delphix. All rights reserved.
 * Copyright 2012 Milan Jurik. All rights reserved.
 * Copyright (c) 2012, Joyent, Inc. All rights reserved.
 * Copyright (c) 2013 Steven Hartland.  All rights reserved.
 * Copyright 2016 Igor Kozhukhov <ikozhukhov@gmail.com>.
 * Copyright 2016 Nexenta Systems, Inc.
 * Copyright (c) 2019 Datto Inc.
 * Copyright (c) 2019, loli10K <ezomori.nozomu@gmail.com>
 * Copyright 2019 Joyent, Inc.
 * Copyright (c) 2019, 2020 by Christian Schwarz. All rights reserved.
 */

#include <assert.h>
#include <ctype.h>
#include <sys/debug.h>
#include <errno.h>
#include <getopt.h>
#include <libgen.h>
#include <libintl.h>
#include <libuutil.h>
#include <libnvpair.h>
#include <locale.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <unistd.h>
#include <fcntl.h>
#include <zone.h>
#include <grp.h>
#include <pwd.h>
#include <signal.h>
#include <sys/list.h>
#include <sys/mkdev.h>
#include <sys/mntent.h>
#include <sys/mnttab.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/fs/zfs.h>
#include <sys/systeminfo.h>
#include <sys/types.h>
#include <time.h>
#include <sys/zfs_project.h>

#include <libzfs.h>
#include <libzfs_core.h>
#include <zfs_prop.h>
#include <zfs_deleg.h>
#include <libzutil.h>
#ifdef HAVE_IDMAP
#include <aclutils.h>
#include <directory.h>
#endif /* HAVE_IDMAP */

#include "zfs_iter.h"
#include "zfs_util.h"
#include "zfs_comutil.h"
#include "libzfs_impl.h"
#include "zfs_projectutil.h"

libzfs_handle_t *g_zfs;

static FILE *mnttab_file;
static char history_str[HIS_MAX_RECORD_LEN];
static boolean_t log_history = B_TRUE;

static int zfs_do_clone(int argc, char **argv);
static int zfs_do_create(int argc, char **argv);
static int zfs_do_destroy(int argc, char **argv);
static int zfs_do_get(int argc, char **argv);
static int zfs_do_inherit(int argc, char **argv);
static int zfs_do_list(int argc, char **argv);
static int zfs_do_mount(int argc, char **argv);
static int zfs_do_rename(int argc, char **argv);
static int zfs_do_rollback(int argc, char **argv);
static int zfs_do_set(int argc, char **argv);
static int zfs_do_upgrade(int argc, char **argv);
static int zfs_do_snapshot(int argc, char **argv);
static int zfs_do_unmount(int argc, char **argv);
static int zfs_do_share(int argc, char **argv);
static int zfs_do_unshare(int argc, char **argv);
static int zfs_do_send(int argc, char **argv);
static int zfs_do_receive(int argc, char **argv);
static int zfs_do_promote(int argc, char **argv);
static int zfs_do_userspace(int argc, char **argv);
static int zfs_do_allow(int argc, char **argv);
static int zfs_do_unallow(int argc, char **argv);
static int zfs_do_hold(int argc, char **argv);
static int zfs_do_holds(int argc, char **argv);
static int zfs_do_release(int argc, char **argv);
static int zfs_do_diff(int argc, char **argv);
static int zfs_do_bookmark(int argc, char **argv);
static int zfs_do_channel_program(int argc, char **argv);
static int zfs_do_load_key(int argc, char **argv);
static int zfs_do_unload_key(int argc, char **argv);
static int zfs_do_change_key(int argc, char **argv);
static int zfs_do_project(int argc, char **argv);
static int zfs_do_version(int argc, char **argv);
static int zfs_do_redact(int argc, char **argv);
static int zfs_do_wait(int argc, char **argv);

#ifdef __FreeBSD__
static int zfs_do_jail(int argc, char **argv);
static int zfs_do_unjail(int argc, char **argv);
#endif

/*
 * Enable a reasonable set of defaults for libumem debugging on DEBUG builds.
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
	HELP_CLONE,
	HELP_CREATE,
	HELP_DESTROY,
	HELP_GET,
	HELP_INHERIT,
	HELP_UPGRADE,
	HELP_LIST,
	HELP_MOUNT,
	HELP_PROMOTE,
	HELP_RECEIVE,
	HELP_RENAME,
	HELP_ROLLBACK,
	HELP_SEND,
	HELP_SET,
	HELP_SHARE,
	HELP_SNAPSHOT,
	HELP_UNMOUNT,
	HELP_UNSHARE,
	HELP_ALLOW,
	HELP_UNALLOW,
	HELP_USERSPACE,
	HELP_GROUPSPACE,
	HELP_PROJECTSPACE,
	HELP_PROJECT,
	HELP_HOLD,
	HELP_HOLDS,
	HELP_RELEASE,
	HELP_DIFF,
	HELP_BOOKMARK,
	HELP_CHANNEL_PROGRAM,
	HELP_LOAD_KEY,
	HELP_UNLOAD_KEY,
	HELP_CHANGE_KEY,
	HELP_VERSION,
	HELP_REDACT,
	HELP_JAIL,
	HELP_UNJAIL,
	HELP_WAIT,
} zfs_help_t;

typedef struct zfs_command {
	const char	*name;
	int		(*func)(int argc, char **argv);
	zfs_help_t	usage;
} zfs_command_t;

/*
 * Master command table.  Each ZFS command has a name, associated function, and
 * usage message.  The usage messages need to be internationalized, so we have
 * to have a function to return the usage message based on a command index.
 *
 * These commands are organized according to how they are displayed in the usage
 * message.  An empty command (one with a NULL name) indicates an empty line in
 * the generic usage message.
 */
static zfs_command_t command_table[] = {
	{ "version",	zfs_do_version, 	HELP_VERSION		},
	{ NULL },
	{ "create",	zfs_do_create,		HELP_CREATE		},
	{ "destroy",	zfs_do_destroy,		HELP_DESTROY		},
	{ NULL },
	{ "snapshot",	zfs_do_snapshot,	HELP_SNAPSHOT		},
	{ "rollback",	zfs_do_rollback,	HELP_ROLLBACK		},
	{ "clone",	zfs_do_clone,		HELP_CLONE		},
	{ "promote",	zfs_do_promote,		HELP_PROMOTE		},
	{ "rename",	zfs_do_rename,		HELP_RENAME		},
	{ "bookmark",	zfs_do_bookmark,	HELP_BOOKMARK		},
	{ "program",    zfs_do_channel_program, HELP_CHANNEL_PROGRAM    },
	{ NULL },
	{ "list",	zfs_do_list,		HELP_LIST		},
	{ NULL },
	{ "set",	zfs_do_set,		HELP_SET		},
	{ "get",	zfs_do_get,		HELP_GET		},
	{ "inherit",	zfs_do_inherit,		HELP_INHERIT		},
	{ "upgrade",	zfs_do_upgrade,		HELP_UPGRADE		},
	{ NULL },
	{ "userspace",	zfs_do_userspace,	HELP_USERSPACE		},
	{ "groupspace",	zfs_do_userspace,	HELP_GROUPSPACE		},
	{ "projectspace", zfs_do_userspace,	HELP_PROJECTSPACE	},
	{ NULL },
	{ "project",	zfs_do_project,		HELP_PROJECT		},
	{ NULL },
	{ "mount",	zfs_do_mount,		HELP_MOUNT		},
	{ "unmount",	zfs_do_unmount,		HELP_UNMOUNT		},
	{ "share",	zfs_do_share,		HELP_SHARE		},
	{ "unshare",	zfs_do_unshare,		HELP_UNSHARE		},
	{ NULL },
	{ "send",	zfs_do_send,		HELP_SEND		},
	{ "receive",	zfs_do_receive,		HELP_RECEIVE		},
	{ NULL },
	{ "allow",	zfs_do_allow,		HELP_ALLOW		},
	{ NULL },
	{ "unallow",	zfs_do_unallow,		HELP_UNALLOW		},
	{ NULL },
	{ "hold",	zfs_do_hold,		HELP_HOLD		},
	{ "holds",	zfs_do_holds,		HELP_HOLDS		},
	{ "release",	zfs_do_release,		HELP_RELEASE		},
	{ "diff",	zfs_do_diff,		HELP_DIFF		},
	{ "load-key",	zfs_do_load_key,	HELP_LOAD_KEY		},
	{ "unload-key",	zfs_do_unload_key,	HELP_UNLOAD_KEY		},
	{ "change-key",	zfs_do_change_key,	HELP_CHANGE_KEY		},
	{ "redact",	zfs_do_redact,		HELP_REDACT		},
	{ "wait",	zfs_do_wait,		HELP_WAIT		},

#ifdef __FreeBSD__
	{ "jail",	zfs_do_jail,		HELP_JAIL		},
	{ "unjail",	zfs_do_unjail,		HELP_UNJAIL		},
#endif
};

#define	NCOMMAND	(sizeof (command_table) / sizeof (command_table[0]))

zfs_command_t *current_command;

static const char *
get_usage(zfs_help_t idx)
{
	switch (idx) {
	case HELP_CLONE:
		return (gettext("\tclone [-p] [-o property=value] ... "
		    "<snapshot> <filesystem|volume>\n"));
	case HELP_CREATE:
		return (gettext("\tcreate [-Pnpuv] [-o property=value] ... "
		    "<filesystem>\n"
		    "\tcreate [-Pnpsv] [-b blocksize] [-o property=value] ... "
		    "-V <size> <volume>\n"));
	case HELP_DESTROY:
		return (gettext("\tdestroy [-fnpRrv] <filesystem|volume>\n"
		    "\tdestroy [-dnpRrv] "
		    "<filesystem|volume>@<snap>[%<snap>][,...]\n"
		    "\tdestroy <filesystem|volume>#<bookmark>\n"));
	case HELP_GET:
		return (gettext("\tget [-rHp] [-d max] "
		    "[-o \"all\" | field[,...]]\n"
		    "\t    [-t type[,...]] [-s source[,...]]\n"
		    "\t    <\"all\" | property[,...]> "
		    "[filesystem|volume|snapshot|bookmark] ...\n"));
	case HELP_INHERIT:
		return (gettext("\tinherit [-rS] <property> "
		    "<filesystem|volume|snapshot> ...\n"));
	case HELP_UPGRADE:
		return (gettext("\tupgrade [-v]\n"
		    "\tupgrade [-r] [-V version] <-a | filesystem ...>\n"));
	case HELP_LIST:
		return (gettext("\tlist [-Hp] [-r|-d max] [-o property[,...]] "
		    "[-s property]...\n\t    [-S property]... [-t type[,...]] "
		    "[filesystem|volume|snapshot] ...\n"));
	case HELP_MOUNT:
		return (gettext("\tmount\n"
		    "\tmount [-flvO] [-o opts] <-a | filesystem>\n"));
	case HELP_PROMOTE:
		return (gettext("\tpromote <clone-filesystem>\n"));
	case HELP_RECEIVE:
		return (gettext("\treceive [-vMnsFhu] "
		    "[-o <property>=<value>] ... [-x <property>] ...\n"
		    "\t    <filesystem|volume|snapshot>\n"
		    "\treceive [-vMnsFhu] [-o <property>=<value>] ... "
		    "[-x <property>] ... \n"
		    "\t    [-d | -e] <filesystem>\n"
		    "\treceive -A <filesystem|volume>\n"));
	case HELP_RENAME:
		return (gettext("\trename [-f] <filesystem|volume|snapshot> "
		    "<filesystem|volume|snapshot>\n"
		    "\trename -p [-f] <filesystem|volume> <filesystem|volume>\n"
		    "\trename -u [-f] <filesystem> <filesystem>\n"
		    "\trename -r <snapshot> <snapshot>\n"));
	case HELP_ROLLBACK:
		return (gettext("\trollback [-rRf] <snapshot>\n"));
	case HELP_SEND:
		return (gettext("\tsend [-DnPpRvLecwhb] [-[i|I] snapshot] "
		    "<snapshot>\n"
		    "\tsend [-DnvPLecw] [-i snapshot|bookmark] "
		    "<filesystem|volume|snapshot>\n"
		    "\tsend [-DnPpvLec] [-i bookmark|snapshot] "
		    "--redact <bookmark> <snapshot>\n"
		    "\tsend [-nvPe] -t <receive_resume_token>\n"
		    "\tsend [-Pnv] --saved filesystem\n"));
	case HELP_SET:
		return (gettext("\tset <property=value> ... "
		    "<filesystem|volume|snapshot> ...\n"));
	case HELP_SHARE:
		return (gettext("\tshare [-l] <-a [nfs|smb] | filesystem>\n"));
	case HELP_SNAPSHOT:
		return (gettext("\tsnapshot [-r] [-o property=value] ... "
		    "<filesystem|volume>@<snap> ...\n"));
	case HELP_UNMOUNT:
		return (gettext("\tunmount [-fu] "
		    "<-a | filesystem|mountpoint>\n"));
	case HELP_UNSHARE:
		return (gettext("\tunshare "
		    "<-a [nfs|smb] | filesystem|mountpoint>\n"));
	case HELP_ALLOW:
		return (gettext("\tallow <filesystem|volume>\n"
		    "\tallow [-ldug] "
		    "<\"everyone\"|user|group>[,...] <perm|@setname>[,...]\n"
		    "\t    <filesystem|volume>\n"
		    "\tallow [-ld] -e <perm|@setname>[,...] "
		    "<filesystem|volume>\n"
		    "\tallow -c <perm|@setname>[,...] <filesystem|volume>\n"
		    "\tallow -s @setname <perm|@setname>[,...] "
		    "<filesystem|volume>\n"));
	case HELP_UNALLOW:
		return (gettext("\tunallow [-rldug] "
		    "<\"everyone\"|user|group>[,...]\n"
		    "\t    [<perm|@setname>[,...]] <filesystem|volume>\n"
		    "\tunallow [-rld] -e [<perm|@setname>[,...]] "
		    "<filesystem|volume>\n"
		    "\tunallow [-r] -c [<perm|@setname>[,...]] "
		    "<filesystem|volume>\n"
		    "\tunallow [-r] -s @setname [<perm|@setname>[,...]] "
		    "<filesystem|volume>\n"));
	case HELP_USERSPACE:
		return (gettext("\tuserspace [-Hinp] [-o field[,...]] "
		    "[-s field] ...\n"
		    "\t    [-S field] ... [-t type[,...]] "
		    "<filesystem|snapshot|path>\n"));
	case HELP_GROUPSPACE:
		return (gettext("\tgroupspace [-Hinp] [-o field[,...]] "
		    "[-s field] ...\n"
		    "\t    [-S field] ... [-t type[,...]] "
		    "<filesystem|snapshot|path>\n"));
	case HELP_PROJECTSPACE:
		return (gettext("\tprojectspace [-Hp] [-o field[,...]] "
		    "[-s field] ... \n"
		    "\t    [-S field] ... <filesystem|snapshot|path>\n"));
	case HELP_PROJECT:
		return (gettext("\tproject [-d|-r] <directory|file ...>\n"
		    "\tproject -c [-0] [-d|-r] [-p id] <directory|file ...>\n"
		    "\tproject -C [-k] [-r] <directory ...>\n"
		    "\tproject [-p id] [-r] [-s] <directory ...>\n"));
	case HELP_HOLD:
		return (gettext("\thold [-r] <tag> <snapshot> ...\n"));
	case HELP_HOLDS:
		return (gettext("\tholds [-rH] <snapshot> ...\n"));
	case HELP_RELEASE:
		return (gettext("\trelease [-r] <tag> <snapshot> ...\n"));
	case HELP_DIFF:
		return (gettext("\tdiff [-FHt] <snapshot> "
		    "[snapshot|filesystem]\n"));
	case HELP_BOOKMARK:
		return (gettext("\tbookmark <snapshot|bookmark> "
		    "<newbookmark>\n"));
	case HELP_CHANNEL_PROGRAM:
		return (gettext("\tprogram [-jn] [-t <instruction limit>] "
		    "[-m <memory limit (b)>]\n"
		    "\t    <pool> <program file> [lua args...]\n"));
	case HELP_LOAD_KEY:
		return (gettext("\tload-key [-rn] [-L <keylocation>] "
		    "<-a | filesystem|volume>\n"));
	case HELP_UNLOAD_KEY:
		return (gettext("\tunload-key [-r] "
		    "<-a | filesystem|volume>\n"));
	case HELP_CHANGE_KEY:
		return (gettext("\tchange-key [-l] [-o keyformat=<value>]\n"
		    "\t    [-o keylocation=<value>] [-o pbkdf2iters=<value>]\n"
		    "\t    <filesystem|volume>\n"
		    "\tchange-key -i [-l] <filesystem|volume>\n"));
	case HELP_VERSION:
		return (gettext("\tversion\n"));
	case HELP_REDACT:
		return (gettext("\tredact <snapshot> <bookmark> "
		    "<redaction_snapshot> ...\n"));
	case HELP_JAIL:
		return (gettext("\tjail <jailid|jailname> <filesystem>\n"));
	case HELP_UNJAIL:
		return (gettext("\tunjail <jailid|jailname> <filesystem>\n"));
	case HELP_WAIT:
		return (gettext("\twait [-t <activity>] <filesystem>\n"));
	}

	abort();
	/* NOTREACHED */
}

void
nomem(void)
{
	(void) fprintf(stderr, gettext("internal error: out of memory\n"));
	exit(1);
}

/*
 * Utility function to guarantee malloc() success.
 */

void *
safe_malloc(size_t size)
{
	void *data;

	if ((data = calloc(1, size)) == NULL)
		nomem();

	return (data);
}

static void *
safe_realloc(void *data, size_t size)
{
	void *newp;
	if ((newp = realloc(data, size)) == NULL) {
		free(data);
		nomem();
	}

	return (newp);
}

static char *
safe_strdup(char *str)
{
	char *dupstr = strdup(str);

	if (dupstr == NULL)
		nomem();

	return (dupstr);
}

/*
 * Callback routine that will print out information for each of
 * the properties.
 */
static int
usage_prop_cb(int prop, void *cb)
{
	FILE *fp = cb;

	(void) fprintf(fp, "\t%-15s ", zfs_prop_to_name(prop));

	if (zfs_prop_readonly(prop))
		(void) fprintf(fp, " NO    ");
	else
		(void) fprintf(fp, "YES    ");

	if (zfs_prop_inheritable(prop))
		(void) fprintf(fp, "  YES   ");
	else
		(void) fprintf(fp, "   NO   ");

	if (zfs_prop_values(prop) == NULL)
		(void) fprintf(fp, "-\n");
	else
		(void) fprintf(fp, "%s\n", zfs_prop_values(prop));

	return (ZPROP_CONT);
}

/*
 * Display usage message.  If we're inside a command, display only the usage for
 * that command.  Otherwise, iterate over the entire command table and display
 * a complete usage message.
 */
static void
usage(boolean_t requested)
{
	int i;
	boolean_t show_properties = B_FALSE;
	FILE *fp = requested ? stdout : stderr;

	if (current_command == NULL) {

		(void) fprintf(fp, gettext("usage: zfs command args ...\n"));
		(void) fprintf(fp,
		    gettext("where 'command' is one of the following:\n\n"));

		for (i = 0; i < NCOMMAND; i++) {
			if (command_table[i].name == NULL)
				(void) fprintf(fp, "\n");
			else
				(void) fprintf(fp, "%s",
				    get_usage(command_table[i].usage));
		}

		(void) fprintf(fp, gettext("\nEach dataset is of the form: "
		    "pool/[dataset/]*dataset[@name]\n"));
	} else {
		(void) fprintf(fp, gettext("usage:\n"));
		(void) fprintf(fp, "%s", get_usage(current_command->usage));
	}

	if (current_command != NULL &&
	    (strcmp(current_command->name, "set") == 0 ||
	    strcmp(current_command->name, "get") == 0 ||
	    strcmp(current_command->name, "inherit") == 0 ||
	    strcmp(current_command->name, "list") == 0))
		show_properties = B_TRUE;

	if (show_properties) {
		(void) fprintf(fp,
		    gettext("\nThe following properties are supported:\n"));

		(void) fprintf(fp, "\n\t%-14s %s  %s   %s\n\n",
		    "PROPERTY", "EDIT", "INHERIT", "VALUES");

		/* Iterate over all properties */
		(void) zprop_iter(usage_prop_cb, fp, B_FALSE, B_TRUE,
		    ZFS_TYPE_DATASET);

		(void) fprintf(fp, "\t%-15s ", "userused@...");
		(void) fprintf(fp, " NO       NO   <size>\n");
		(void) fprintf(fp, "\t%-15s ", "groupused@...");
		(void) fprintf(fp, " NO       NO   <size>\n");
		(void) fprintf(fp, "\t%-15s ", "projectused@...");
		(void) fprintf(fp, " NO       NO   <size>\n");
		(void) fprintf(fp, "\t%-15s ", "userobjused@...");
		(void) fprintf(fp, " NO       NO   <size>\n");
		(void) fprintf(fp, "\t%-15s ", "groupobjused@...");
		(void) fprintf(fp, " NO       NO   <size>\n");
		(void) fprintf(fp, "\t%-15s ", "projectobjused@...");
		(void) fprintf(fp, " NO       NO   <size>\n");
		(void) fprintf(fp, "\t%-15s ", "userquota@...");
		(void) fprintf(fp, "YES       NO   <size> | none\n");
		(void) fprintf(fp, "\t%-15s ", "groupquota@...");
		(void) fprintf(fp, "YES       NO   <size> | none\n");
		(void) fprintf(fp, "\t%-15s ", "projectquota@...");
		(void) fprintf(fp, "YES       NO   <size> | none\n");
		(void) fprintf(fp, "\t%-15s ", "userobjquota@...");
		(void) fprintf(fp, "YES       NO   <size> | none\n");
		(void) fprintf(fp, "\t%-15s ", "groupobjquota@...");
		(void) fprintf(fp, "YES       NO   <size> | none\n");
		(void) fprintf(fp, "\t%-15s ", "projectobjquota@...");
		(void) fprintf(fp, "YES       NO   <size> | none\n");
		(void) fprintf(fp, "\t%-15s ", "written@<snap>");
		(void) fprintf(fp, " NO       NO   <size>\n");
		(void) fprintf(fp, "\t%-15s ", "written#<bookmark>");
		(void) fprintf(fp, " NO       NO   <size>\n");

		(void) fprintf(fp, gettext("\nSizes are specified in bytes "
		    "with standard units such as K, M, G, etc.\n"));
		(void) fprintf(fp, gettext("\nUser-defined properties can "
		    "be specified by using a name containing a colon (:).\n"));
		(void) fprintf(fp, gettext("\nThe {user|group|project}"
		    "[obj]{used|quota}@ properties must be appended with\n"
		    "a user|group|project specifier of one of these forms:\n"
		    "    POSIX name      (eg: \"matt\")\n"
		    "    POSIX id        (eg: \"126829\")\n"
		    "    SMB name@domain (eg: \"matt@sun\")\n"
		    "    SMB SID         (eg: \"S-1-234-567-89\")\n"));
	} else {
		(void) fprintf(fp,
		    gettext("\nFor the property list, run: %s\n"),
		    "zfs set|get");
		(void) fprintf(fp,
		    gettext("\nFor the delegated permission list, run: %s\n"),
		    "zfs allow|unallow");
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

/*
 * Take a property=value argument string and add it to the given nvlist.
 * Modifies the argument inplace.
 */
static boolean_t
parseprop(nvlist_t *props, char *propname)
{
	char *propval;

	if ((propval = strchr(propname, '=')) == NULL) {
		(void) fprintf(stderr, gettext("missing "
		    "'=' for property=value argument\n"));
		return (B_FALSE);
	}
	*propval = '\0';
	propval++;
	if (nvlist_exists(props, propname)) {
		(void) fprintf(stderr, gettext("property '%s' "
		    "specified multiple times\n"), propname);
		return (B_FALSE);
	}
	if (nvlist_add_string(props, propname, propval) != 0)
		nomem();
	return (B_TRUE);
}

/*
 * Take a property name argument and add it to the given nvlist.
 * Modifies the argument inplace.
 */
static boolean_t
parsepropname(nvlist_t *props, char *propname)
{
	if (strchr(propname, '=') != NULL) {
		(void) fprintf(stderr, gettext("invalid character "
		    "'=' in property argument\n"));
		return (B_FALSE);
	}
	if (nvlist_exists(props, propname)) {
		(void) fprintf(stderr, gettext("property '%s' "
		    "specified multiple times\n"), propname);
		return (B_FALSE);
	}
	if (nvlist_add_boolean(props, propname) != 0)
		nomem();
	return (B_TRUE);
}

static int
parse_depth(char *opt, int *flags)
{
	char *tmp;
	int depth;

	depth = (int)strtol(opt, &tmp, 0);
	if (*tmp) {
		(void) fprintf(stderr,
		    gettext("%s is not an integer\n"), optarg);
		usage(B_FALSE);
	}
	if (depth < 0) {
		(void) fprintf(stderr,
		    gettext("Depth can not be negative.\n"));
		usage(B_FALSE);
	}
	*flags |= (ZFS_ITER_DEPTH_LIMIT|ZFS_ITER_RECURSE);
	return (depth);
}

#define	PROGRESS_DELAY 2		/* seconds */

static char *pt_reverse = "\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b";
static time_t pt_begin;
static char *pt_header = NULL;
static boolean_t pt_shown;

static void
start_progress_timer(void)
{
	pt_begin = time(NULL) + PROGRESS_DELAY;
	pt_shown = B_FALSE;
}

static void
set_progress_header(char *header)
{
	assert(pt_header == NULL);
	pt_header = safe_strdup(header);
	if (pt_shown) {
		(void) printf("%s: ", header);
		(void) fflush(stdout);
	}
}

static void
update_progress(char *update)
{
	if (!pt_shown && time(NULL) > pt_begin) {
		int len = strlen(update);

		(void) printf("%s: %s%*.*s", pt_header, update, len, len,
		    pt_reverse);
		(void) fflush(stdout);
		pt_shown = B_TRUE;
	} else if (pt_shown) {
		int len = strlen(update);

		(void) printf("%s%*.*s", update, len, len, pt_reverse);
		(void) fflush(stdout);
	}
}

static void
finish_progress(char *done)
{
	if (pt_shown) {
		(void) printf("%s\n", done);
		(void) fflush(stdout);
	}
	free(pt_header);
	pt_header = NULL;
}

/* This function checks if the passed fd refers to /dev/null or /dev/zero */
#ifdef __linux__
static boolean_t
is_dev_nullzero(int fd)
{
	struct stat st;
	fstat(fd, &st);
	return (major(st.st_rdev) == 1 && (minor(st.st_rdev) == 3 /* null */ ||
	    minor(st.st_rdev) == 5 /* zero */));
}
#endif

static void
note_dev_error(int err, int fd)
{
#ifdef __linux__
	if (err == EINVAL && is_dev_nullzero(fd)) {
		(void) fprintf(stderr,
		    gettext("Error: Writing directly to /dev/{null,zero} files"
		    " on certain kernels is not currently implemented.\n"
		    "(As a workaround, "
		    "try \"zfs send [...] | cat > /dev/null\")\n"));
	}
#endif
}

static int
zfs_mount_and_share(libzfs_handle_t *hdl, const char *dataset, zfs_type_t type)
{
	zfs_handle_t *zhp = NULL;
	int ret = 0;

	zhp = zfs_open(hdl, dataset, type);
	if (zhp == NULL)
		return (1);

	/*
	 * Volumes may neither be mounted or shared.  Potentially in the
	 * future filesystems detected on these volumes could be mounted.
	 */
	if (zfs_get_type(zhp) == ZFS_TYPE_VOLUME) {
		zfs_close(zhp);
		return (0);
	}

	/*
	 * Mount and/or share the new filesystem as appropriate.  We provide a
	 * verbose error message to let the user know that their filesystem was
	 * in fact created, even if we failed to mount or share it.
	 *
	 * If the user doesn't want the dataset automatically mounted, then
	 * skip the mount/share step
	 */
	if (zfs_prop_valid_for_type(ZFS_PROP_CANMOUNT, type, B_FALSE) &&
	    zfs_prop_get_int(zhp, ZFS_PROP_CANMOUNT) == ZFS_CANMOUNT_ON) {
		if (zfs_mount_delegation_check()) {
			(void) fprintf(stderr, gettext("filesystem "
			    "successfully created, but it may only be "
			    "mounted by root\n"));
			ret = 1;
		} else if (zfs_mount(zhp, NULL, 0) != 0) {
			(void) fprintf(stderr, gettext("filesystem "
			    "successfully created, but not mounted\n"));
			ret = 1;
		} else if (zfs_share(zhp) != 0) {
			(void) fprintf(stderr, gettext("filesystem "
			    "successfully created, but not shared\n"));
			ret = 1;
		}
		zfs_commit_all_shares();
	}

	zfs_close(zhp);

	return (ret);
}

/*
 * zfs clone [-p] [-o prop=value] ... <snap> <fs | vol>
 *
 * Given an existing dataset, create a writable copy whose initial contents
 * are the same as the source.  The newly created dataset maintains a
 * dependency on the original; the original cannot be destroyed so long as
 * the clone exists.
 *
 * The '-p' flag creates all the non-existing ancestors of the target first.
 */
static int
zfs_do_clone(int argc, char **argv)
{
	zfs_handle_t *zhp = NULL;
	boolean_t parents = B_FALSE;
	nvlist_t *props;
	int ret = 0;
	int c;

	if (nvlist_alloc(&props, NV_UNIQUE_NAME, 0) != 0)
		nomem();

	/* check options */
	while ((c = getopt(argc, argv, "o:p")) != -1) {
		switch (c) {
		case 'o':
			if (!parseprop(props, optarg)) {
				nvlist_free(props);
				return (1);
			}
			break;
		case 'p':
			parents = B_TRUE;
			break;
		case '?':
			(void) fprintf(stderr, gettext("invalid option '%c'\n"),
			    optopt);
			goto usage;
		}
	}

	argc -= optind;
	argv += optind;

	/* check number of arguments */
	if (argc < 1) {
		(void) fprintf(stderr, gettext("missing source dataset "
		    "argument\n"));
		goto usage;
	}
	if (argc < 2) {
		(void) fprintf(stderr, gettext("missing target dataset "
		    "argument\n"));
		goto usage;
	}
	if (argc > 2) {
		(void) fprintf(stderr, gettext("too many arguments\n"));
		goto usage;
	}

	/* open the source dataset */
	if ((zhp = zfs_open(g_zfs, argv[0], ZFS_TYPE_SNAPSHOT)) == NULL) {
		nvlist_free(props);
		return (1);
	}

	if (parents && zfs_name_valid(argv[1], ZFS_TYPE_FILESYSTEM |
	    ZFS_TYPE_VOLUME)) {
		/*
		 * Now create the ancestors of the target dataset.  If the
		 * target already exists and '-p' option was used we should not
		 * complain.
		 */
		if (zfs_dataset_exists(g_zfs, argv[1], ZFS_TYPE_FILESYSTEM |
		    ZFS_TYPE_VOLUME)) {
			zfs_close(zhp);
			nvlist_free(props);
			return (0);
		}
		if (zfs_create_ancestors(g_zfs, argv[1]) != 0) {
			zfs_close(zhp);
			nvlist_free(props);
			return (1);
		}
	}

	/* pass to libzfs */
	ret = zfs_clone(zhp, argv[1], props);

	/* create the mountpoint if necessary */
	if (ret == 0) {
		if (log_history) {
			(void) zpool_log_history(g_zfs, history_str);
			log_history = B_FALSE;
		}

		ret = zfs_mount_and_share(g_zfs, argv[1], ZFS_TYPE_DATASET);
	}

	zfs_close(zhp);
	nvlist_free(props);

	return (!!ret);

usage:
	ASSERT3P(zhp, ==, NULL);
	nvlist_free(props);
	usage(B_FALSE);
	return (-1);
}

/*
 * Return a default volblocksize for the pool which always uses more than
 * half of the data sectors.  This primarily applies to dRAID which always
 * writes full stripe widths.
 */
static uint64_t
default_volblocksize(zpool_handle_t *zhp, nvlist_t *props)
{
	uint64_t volblocksize, asize = SPA_MINBLOCKSIZE;
	nvlist_t *tree, **vdevs;
	uint_t nvdevs;

	nvlist_t *config = zpool_get_config(zhp, NULL);

	if (nvlist_lookup_nvlist(config, ZPOOL_CONFIG_VDEV_TREE, &tree) != 0 ||
	    nvlist_lookup_nvlist_array(tree, ZPOOL_CONFIG_CHILDREN,
	    &vdevs, &nvdevs) != 0) {
		return (ZVOL_DEFAULT_BLOCKSIZE);
	}

	for (int i = 0; i < nvdevs; i++) {
		nvlist_t *nv = vdevs[i];
		uint64_t ashift, ndata, nparity;

		if (nvlist_lookup_uint64(nv, ZPOOL_CONFIG_ASHIFT, &ashift) != 0)
			continue;

		if (nvlist_lookup_uint64(nv, ZPOOL_CONFIG_DRAID_NDATA,
		    &ndata) == 0) {
			/* dRAID minimum allocation width */
			asize = MAX(asize, ndata * (1ULL << ashift));
		} else if (nvlist_lookup_uint64(nv, ZPOOL_CONFIG_NPARITY,
		    &nparity) == 0) {
			/* raidz minimum allocation width */
			if (nparity == 1)
				asize = MAX(asize, 2 * (1ULL << ashift));
			else
				asize = MAX(asize, 4 * (1ULL << ashift));
		} else {
			/* mirror or (non-redundant) leaf vdev */
			asize = MAX(asize, 1ULL << ashift);
		}
	}

	/*
	 * Calculate the target volblocksize such that more than half
	 * of the asize is used. The following table is for 4k sectors.
	 *
	 * n   asize   blksz  used  |   n   asize   blksz  used
	 * -------------------------+---------------------------------
	 * 1   4,096   8,192  100%  |   9  36,864  32,768   88%
	 * 2   8,192   8,192  100%  |  10  40,960  32,768   80%
	 * 3  12,288   8,192   66%  |  11  45,056  32,768   72%
	 * 4  16,384  16,384  100%  |  12  49,152  32,768   66%
	 * 5  20,480  16,384   80%  |  13  53,248  32,768   61%
	 * 6  24,576  16,384   66%  |  14  57,344  32,768   57%
	 * 7  28,672  16,384   57%  |  15  61,440  32,768   53%
	 * 8  32,768  32,768  100%  |  16  65,536  65,636  100%
	 *
	 * This is primarily a concern for dRAID which always allocates
	 * a full stripe width.  For dRAID the default stripe width is
	 * n=8 in which case the volblocksize is set to 32k. Ignoring
	 * compression there are no unused sectors.  This same reasoning
	 * applies to raidz[2,3] so target 4 sectors to minimize waste.
	 */
	uint64_t tgt_volblocksize = ZVOL_DEFAULT_BLOCKSIZE;
	while (tgt_volblocksize * 2 <= asize)
		tgt_volblocksize *= 2;

	const char *prop = zfs_prop_to_name(ZFS_PROP_VOLBLOCKSIZE);
	if (nvlist_lookup_uint64(props, prop, &volblocksize) == 0) {

		/* Issue a warning when a non-optimal size is requested. */
		if (volblocksize < ZVOL_DEFAULT_BLOCKSIZE) {
			(void) fprintf(stderr, gettext("Warning: "
			    "volblocksize (%llu) is less than the default "
			    "minimum block size (%llu).\nTo reduce wasted "
			    "space a volblocksize of %llu is recommended.\n"),
			    (u_longlong_t)volblocksize,
			    (u_longlong_t)ZVOL_DEFAULT_BLOCKSIZE,
			    (u_longlong_t)tgt_volblocksize);
		} else if (volblocksize < tgt_volblocksize) {
			(void) fprintf(stderr, gettext("Warning: "
			    "volblocksize (%llu) is much less than the "
			    "minimum allocation\nunit (%llu), which wastes "
			    "at least %llu%% of space. To reduce wasted "
			    "space,\nuse a larger volblocksize (%llu is "
			    "recommended), fewer dRAID data disks\n"
			    "per group, or smaller sector size (ashift).\n"),
			    (u_longlong_t)volblocksize, (u_longlong_t)asize,
			    (u_longlong_t)((100 * (asize - volblocksize)) /
			    asize), (u_longlong_t)tgt_volblocksize);
		}
	} else {
		volblocksize = tgt_volblocksize;
		fnvlist_add_uint64(props, prop, volblocksize);
	}

	return (volblocksize);
}

/*
 * zfs create [-Pnpv] [-o prop=value] ... fs
 * zfs create [-Pnpsv] [-b blocksize] [-o prop=value] ... -V vol size
 *
 * Create a new dataset.  This command can be used to create filesystems
 * and volumes.  Snapshot creation is handled by 'zfs snapshot'.
 * For volumes, the user must specify a size to be used.
 *
 * The '-s' flag applies only to volumes, and indicates that we should not try
 * to set the reservation for this volume.  By default we set a reservation
 * equal to the size for any volume.  For pools with SPA_VERSION >=
 * SPA_VERSION_REFRESERVATION, we set a refreservation instead.
 *
 * The '-p' flag creates all the non-existing ancestors of the target first.
 *
 * The '-n' flag is no-op (dry run) mode.  This will perform a user-space sanity
 * check of arguments and properties, but does not check for permissions,
 * available space, etc.
 *
 * The '-u' flag prevents the newly created file system from being mounted.
 *
 * The '-v' flag is for verbose output.
 *
 * The '-P' flag is used for parseable output.  It implies '-v'.
 */
static int
zfs_do_create(int argc, char **argv)
{
	zfs_type_t type = ZFS_TYPE_FILESYSTEM;
	zpool_handle_t *zpool_handle = NULL;
	nvlist_t *real_props = NULL;
	uint64_t volsize = 0;
	int c;
	boolean_t noreserve = B_FALSE;
	boolean_t bflag = B_FALSE;
	boolean_t parents = B_FALSE;
	boolean_t dryrun = B_FALSE;
	boolean_t nomount = B_FALSE;
	boolean_t verbose = B_FALSE;
	boolean_t parseable = B_FALSE;
	int ret = 1;
	nvlist_t *props;
	uint64_t intval;
	char *strval;

	if (nvlist_alloc(&props, NV_UNIQUE_NAME, 0) != 0)
		nomem();

	/* check options */
	while ((c = getopt(argc, argv, ":PV:b:nso:puv")) != -1) {
		switch (c) {
		case 'V':
			type = ZFS_TYPE_VOLUME;
			if (zfs_nicestrtonum(g_zfs, optarg, &intval) != 0) {
				(void) fprintf(stderr, gettext("bad volume "
				    "size '%s': %s\n"), optarg,
				    libzfs_error_description(g_zfs));
				goto error;
			}

			if (nvlist_add_uint64(props,
			    zfs_prop_to_name(ZFS_PROP_VOLSIZE), intval) != 0)
				nomem();
			volsize = intval;
			break;
		case 'P':
			verbose = B_TRUE;
			parseable = B_TRUE;
			break;
		case 'p':
			parents = B_TRUE;
			break;
		case 'b':
			bflag = B_TRUE;
			if (zfs_nicestrtonum(g_zfs, optarg, &intval) != 0) {
				(void) fprintf(stderr, gettext("bad volume "
				    "block size '%s': %s\n"), optarg,
				    libzfs_error_description(g_zfs));
				goto error;
			}

			if (nvlist_add_uint64(props,
			    zfs_prop_to_name(ZFS_PROP_VOLBLOCKSIZE),
			    intval) != 0)
				nomem();
			break;
		case 'n':
			dryrun = B_TRUE;
			break;
		case 'o':
			if (!parseprop(props, optarg))
				goto error;
			break;
		case 's':
			noreserve = B_TRUE;
			break;
		case 'u':
			nomount = B_TRUE;
			break;
		case 'v':
			verbose = B_TRUE;
			break;
		case ':':
			(void) fprintf(stderr, gettext("missing size "
			    "argument\n"));
			goto badusage;
		case '?':
			(void) fprintf(stderr, gettext("invalid option '%c'\n"),
			    optopt);
			goto badusage;
		}
	}

	if ((bflag || noreserve) && type != ZFS_TYPE_VOLUME) {
		(void) fprintf(stderr, gettext("'-s' and '-b' can only be "
		    "used when creating a volume\n"));
		goto badusage;
	}
	if (nomount && type != ZFS_TYPE_FILESYSTEM) {
		(void) fprintf(stderr, gettext("'-u' can only be "
		    "used when creating a filesystem\n"));
		goto badusage;
	}

	argc -= optind;
	argv += optind;

	/* check number of arguments */
	if (argc == 0) {
		(void) fprintf(stderr, gettext("missing %s argument\n"),
		    zfs_type_to_name(type));
		goto badusage;
	}
	if (argc > 1) {
		(void) fprintf(stderr, gettext("too many arguments\n"));
		goto badusage;
	}

	if (dryrun || type == ZFS_TYPE_VOLUME) {
		char msg[ZFS_MAX_DATASET_NAME_LEN * 2];
		char *p;

		if ((p = strchr(argv[0], '/')) != NULL)
			*p = '\0';
		zpool_handle = zpool_open(g_zfs, argv[0]);
		if (p != NULL)
			*p = '/';
		if (zpool_handle == NULL)
			goto error;

		(void) snprintf(msg, sizeof (msg),
		    dryrun ? gettext("cannot verify '%s'") :
		    gettext("cannot create '%s'"), argv[0]);
		if (props && (real_props = zfs_valid_proplist(g_zfs, type,
		    props, 0, NULL, zpool_handle, B_TRUE, msg)) == NULL) {
			zpool_close(zpool_handle);
			goto error;
		}
	}

	if (type == ZFS_TYPE_VOLUME) {
		const char *prop = zfs_prop_to_name(ZFS_PROP_VOLBLOCKSIZE);
		uint64_t volblocksize = default_volblocksize(zpool_handle,
		    real_props);

		if (volblocksize != ZVOL_DEFAULT_BLOCKSIZE &&
		    nvlist_lookup_string(props, prop, &strval) != 0) {
			if (asprintf(&strval, "%llu",
			    (u_longlong_t)volblocksize) == -1)
				nomem();
			nvlist_add_string(props, prop, strval);
			free(strval);
		}

		/*
		 * If volsize is not a multiple of volblocksize, round it
		 * up to the nearest multiple of the volblocksize.
		 */
		if (volsize % volblocksize) {
			volsize = P2ROUNDUP_TYPED(volsize, volblocksize,
			    uint64_t);

			if (nvlist_add_uint64(props,
			    zfs_prop_to_name(ZFS_PROP_VOLSIZE), volsize) != 0) {
				nvlist_free(props);
				nomem();
			}
		}
	}

	if (type == ZFS_TYPE_VOLUME && !noreserve) {
		uint64_t spa_version;
		zfs_prop_t resv_prop;

		spa_version = zpool_get_prop_int(zpool_handle,
		    ZPOOL_PROP_VERSION, NULL);
		if (spa_version >= SPA_VERSION_REFRESERVATION)
			resv_prop = ZFS_PROP_REFRESERVATION;
		else
			resv_prop = ZFS_PROP_RESERVATION;

		volsize = zvol_volsize_to_reservation(zpool_handle, volsize,
		    real_props);

		if (nvlist_lookup_string(props, zfs_prop_to_name(resv_prop),
		    &strval) != 0) {
			if (nvlist_add_uint64(props,
			    zfs_prop_to_name(resv_prop), volsize) != 0) {
				nvlist_free(props);
				nomem();
			}
		}
	}
	if (zpool_handle != NULL) {
		zpool_close(zpool_handle);
		nvlist_free(real_props);
	}

	if (parents && zfs_name_valid(argv[0], type)) {
		/*
		 * Now create the ancestors of target dataset.  If the target
		 * already exists and '-p' option was used we should not
		 * complain.
		 */
		if (zfs_dataset_exists(g_zfs, argv[0], type)) {
			ret = 0;
			goto error;
		}
		if (verbose) {
			(void) printf(parseable ? "create_ancestors\t%s\n" :
			    dryrun ?  "would create ancestors of %s\n" :
			    "create ancestors of %s\n", argv[0]);
		}
		if (!dryrun) {
			if (zfs_create_ancestors(g_zfs, argv[0]) != 0) {
				goto error;
			}
		}
	}

	if (verbose) {
		nvpair_t *nvp = NULL;
		(void) printf(parseable ? "create\t%s\n" :
		    dryrun ? "would create %s\n" : "create %s\n", argv[0]);
		while ((nvp = nvlist_next_nvpair(props, nvp)) != NULL) {
			uint64_t uval;
			char *sval;

			switch (nvpair_type(nvp)) {
			case DATA_TYPE_UINT64:
				VERIFY0(nvpair_value_uint64(nvp, &uval));
				(void) printf(parseable ?
				    "property\t%s\t%llu\n" : "\t%s=%llu\n",
				    nvpair_name(nvp), (u_longlong_t)uval);
				break;
			case DATA_TYPE_STRING:
				VERIFY0(nvpair_value_string(nvp, &sval));
				(void) printf(parseable ?
				    "property\t%s\t%s\n" : "\t%s=%s\n",
				    nvpair_name(nvp), sval);
				break;
			default:
				(void) fprintf(stderr, "property '%s' "
				    "has illegal type %d\n",
				    nvpair_name(nvp), nvpair_type(nvp));
				abort();
			}
		}
	}
	if (dryrun) {
		ret = 0;
		goto error;
	}

	/* pass to libzfs */
	if (zfs_create(g_zfs, argv[0], type, props) != 0)
		goto error;

	if (log_history) {
		(void) zpool_log_history(g_zfs, history_str);
		log_history = B_FALSE;
	}

	if (nomount) {
		ret = 0;
		goto error;
	}

	ret = zfs_mount_and_share(g_zfs, argv[0], ZFS_TYPE_DATASET);
error:
	nvlist_free(props);
	return (ret);
badusage:
	nvlist_free(props);
	usage(B_FALSE);
	return (2);
}

/*
 * zfs destroy [-rRf] <fs, vol>
 * zfs destroy [-rRd] <snap>
 *
 *	-r	Recursively destroy all children
 *	-R	Recursively destroy all dependents, including clones
 *	-f	Force unmounting of any dependents
 *	-d	If we can't destroy now, mark for deferred destruction
 *
 * Destroys the given dataset.  By default, it will unmount any filesystems,
 * and refuse to destroy a dataset that has any dependents.  A dependent can
 * either be a child, or a clone of a child.
 */
typedef struct destroy_cbdata {
	boolean_t	cb_first;
	boolean_t	cb_force;
	boolean_t	cb_recurse;
	boolean_t	cb_error;
	boolean_t	cb_doclones;
	zfs_handle_t	*cb_target;
	boolean_t	cb_defer_destroy;
	boolean_t	cb_verbose;
	boolean_t	cb_parsable;
	boolean_t	cb_dryrun;
	nvlist_t	*cb_nvl;
	nvlist_t	*cb_batchedsnaps;

	/* first snap in contiguous run */
	char		*cb_firstsnap;
	/* previous snap in contiguous run */
	char		*cb_prevsnap;
	int64_t		cb_snapused;
	char		*cb_snapspec;
	char		*cb_bookmark;
	uint64_t	cb_snap_count;
} destroy_cbdata_t;

/*
 * Check for any dependents based on the '-r' or '-R' flags.
 */
static int
destroy_check_dependent(zfs_handle_t *zhp, void *data)
{
	destroy_cbdata_t *cbp = data;
	const char *tname = zfs_get_name(cbp->cb_target);
	const char *name = zfs_get_name(zhp);

	if (strncmp(tname, name, strlen(tname)) == 0 &&
	    (name[strlen(tname)] == '/' || name[strlen(tname)] == '@')) {
		/*
		 * This is a direct descendant, not a clone somewhere else in
		 * the hierarchy.
		 */
		if (cbp->cb_recurse)
			goto out;

		if (cbp->cb_first) {
			(void) fprintf(stderr, gettext("cannot destroy '%s': "
			    "%s has children\n"),
			    zfs_get_name(cbp->cb_target),
			    zfs_type_to_name(zfs_get_type(cbp->cb_target)));
			(void) fprintf(stderr, gettext("use '-r' to destroy "
			    "the following datasets:\n"));
			cbp->cb_first = B_FALSE;
			cbp->cb_error = B_TRUE;
		}

		(void) fprintf(stderr, "%s\n", zfs_get_name(zhp));
	} else {
		/*
		 * This is a clone.  We only want to report this if the '-r'
		 * wasn't specified, or the target is a snapshot.
		 */
		if (!cbp->cb_recurse &&
		    zfs_get_type(cbp->cb_target) != ZFS_TYPE_SNAPSHOT)
			goto out;

		if (cbp->cb_first) {
			(void) fprintf(stderr, gettext("cannot destroy '%s': "
			    "%s has dependent clones\n"),
			    zfs_get_name(cbp->cb_target),
			    zfs_type_to_name(zfs_get_type(cbp->cb_target)));
			(void) fprintf(stderr, gettext("use '-R' to destroy "
			    "the following datasets:\n"));
			cbp->cb_first = B_FALSE;
			cbp->cb_error = B_TRUE;
			cbp->cb_dryrun = B_TRUE;
		}

		(void) fprintf(stderr, "%s\n", zfs_get_name(zhp));
	}

out:
	zfs_close(zhp);
	return (0);
}

static int
destroy_batched(destroy_cbdata_t *cb)
{
	int error = zfs_destroy_snaps_nvl(g_zfs,
	    cb->cb_batchedsnaps, B_FALSE);
	fnvlist_free(cb->cb_batchedsnaps);
	cb->cb_batchedsnaps = fnvlist_alloc();
	return (error);
}

static int
destroy_callback(zfs_handle_t *zhp, void *data)
{
	destroy_cbdata_t *cb = data;
	const char *name = zfs_get_name(zhp);
	int error;

	if (cb->cb_verbose) {
		if (cb->cb_parsable) {
			(void) printf("destroy\t%s\n", name);
		} else if (cb->cb_dryrun) {
			(void) printf(gettext("would destroy %s\n"),
			    name);
		} else {
			(void) printf(gettext("will destroy %s\n"),
			    name);
		}
	}

	/*
	 * Ignore pools (which we've already flagged as an error before getting
	 * here).
	 */
	if (strchr(zfs_get_name(zhp), '/') == NULL &&
	    zfs_get_type(zhp) == ZFS_TYPE_FILESYSTEM) {
		zfs_close(zhp);
		return (0);
	}
	if (cb->cb_dryrun) {
		zfs_close(zhp);
		return (0);
	}

	/*
	 * We batch up all contiguous snapshots (even of different
	 * filesystems) and destroy them with one ioctl.  We can't
	 * simply do all snap deletions and then all fs deletions,
	 * because we must delete a clone before its origin.
	 */
	if (zfs_get_type(zhp) == ZFS_TYPE_SNAPSHOT) {
		cb->cb_snap_count++;
		fnvlist_add_boolean(cb->cb_batchedsnaps, name);
		if (cb->cb_snap_count % 10 == 0 && cb->cb_defer_destroy)
			error = destroy_batched(cb);
	} else {
		error = destroy_batched(cb);
		if (error != 0 ||
		    zfs_unmount(zhp, NULL, cb->cb_force ? MS_FORCE : 0) != 0 ||
		    zfs_destroy(zhp, cb->cb_defer_destroy) != 0) {
			zfs_close(zhp);
			/*
			 * When performing a recursive destroy we ignore errors
			 * so that the recursive destroy could continue
			 * destroying past problem datasets
			 */
			if (cb->cb_recurse) {
				cb->cb_error = B_TRUE;
				return (0);
			}
			return (-1);
		}
	}

	zfs_close(zhp);
	return (0);
}

static int
destroy_print_cb(zfs_handle_t *zhp, void *arg)
{
	destroy_cbdata_t *cb = arg;
	const char *name = zfs_get_name(zhp);
	int err = 0;

	if (nvlist_exists(cb->cb_nvl, name)) {
		if (cb->cb_firstsnap == NULL)
			cb->cb_firstsnap = strdup(name);
		if (cb->cb_prevsnap != NULL)
			free(cb->cb_prevsnap);
		/* this snap continues the current range */
		cb->cb_prevsnap = strdup(name);
		if (cb->cb_firstsnap == NULL || cb->cb_prevsnap == NULL)
			nomem();
		if (cb->cb_verbose) {
			if (cb->cb_parsable) {
				(void) printf("destroy\t%s\n", name);
			} else if (cb->cb_dryrun) {
				(void) printf(gettext("would destroy %s\n"),
				    name);
			} else {
				(void) printf(gettext("will destroy %s\n"),
				    name);
			}
		}
	} else if (cb->cb_firstsnap != NULL) {
		/* end of this range */
		uint64_t used = 0;
		err = lzc_snaprange_space(cb->cb_firstsnap,
		    cb->cb_prevsnap, &used);
		cb->cb_snapused += used;
		free(cb->cb_firstsnap);
		cb->cb_firstsnap = NULL;
		free(cb->cb_prevsnap);
		cb->cb_prevsnap = NULL;
	}
	zfs_close(zhp);
	return (err);
}

static int
destroy_print_snapshots(zfs_handle_t *fs_zhp, destroy_cbdata_t *cb)
{
	int err;
	assert(cb->cb_firstsnap == NULL);
	assert(cb->cb_prevsnap == NULL);
	err = zfs_iter_snapshots_sorted(fs_zhp, destroy_print_cb, cb, 0, 0);
	if (cb->cb_firstsnap != NULL) {
		uint64_t used = 0;
		if (err == 0) {
			err = lzc_snaprange_space(cb->cb_firstsnap,
			    cb->cb_prevsnap, &used);
		}
		cb->cb_snapused += used;
		free(cb->cb_firstsnap);
		cb->cb_firstsnap = NULL;
		free(cb->cb_prevsnap);
		cb->cb_prevsnap = NULL;
	}
	return (err);
}

static int
snapshot_to_nvl_cb(zfs_handle_t *zhp, void *arg)
{
	destroy_cbdata_t *cb = arg;
	int err = 0;

	/* Check for clones. */
	if (!cb->cb_doclones && !cb->cb_defer_destroy) {
		cb->cb_target = zhp;
		cb->cb_first = B_TRUE;
		err = zfs_iter_dependents(zhp, B_TRUE,
		    destroy_check_dependent, cb);
	}

	if (err == 0) {
		if (nvlist_add_boolean(cb->cb_nvl, zfs_get_name(zhp)))
			nomem();
	}
	zfs_close(zhp);
	return (err);
}

static int
gather_snapshots(zfs_handle_t *zhp, void *arg)
{
	destroy_cbdata_t *cb = arg;
	int err = 0;

	err = zfs_iter_snapspec(zhp, cb->cb_snapspec, snapshot_to_nvl_cb, cb);
	if (err == ENOENT)
		err = 0;
	if (err != 0)
		goto out;

	if (cb->cb_verbose) {
		err = destroy_print_snapshots(zhp, cb);
		if (err != 0)
			goto out;
	}

	if (cb->cb_recurse)
		err = zfs_iter_filesystems(zhp, gather_snapshots, cb);

out:
	zfs_close(zhp);
	return (err);
}

static int
destroy_clones(destroy_cbdata_t *cb)
{
	nvpair_t *pair;
	for (pair = nvlist_next_nvpair(cb->cb_nvl, NULL);
	    pair != NULL;
	    pair = nvlist_next_nvpair(cb->cb_nvl, pair)) {
		zfs_handle_t *zhp = zfs_open(g_zfs, nvpair_name(pair),
		    ZFS_TYPE_SNAPSHOT);
		if (zhp != NULL) {
			boolean_t defer = cb->cb_defer_destroy;
			int err;

			/*
			 * We can't defer destroy non-snapshots, so set it to
			 * false while destroying the clones.
			 */
			cb->cb_defer_destroy = B_FALSE;
			err = zfs_iter_dependents(zhp, B_FALSE,
			    destroy_callback, cb);
			cb->cb_defer_destroy = defer;
			zfs_close(zhp);
			if (err != 0)
				return (err);
		}
	}
	return (0);
}

static int
zfs_do_destroy(int argc, char **argv)
{
	destroy_cbdata_t cb = { 0 };
	int rv = 0;
	int err = 0;
	int c;
	zfs_handle_t *zhp = NULL;
	char *at, *pound;
	zfs_type_t type = ZFS_TYPE_DATASET;

	/* check options */
	while ((c = getopt(argc, argv, "vpndfrR")) != -1) {
		switch (c) {
		case 'v':
			cb.cb_verbose = B_TRUE;
			break;
		case 'p':
			cb.cb_verbose = B_TRUE;
			cb.cb_parsable = B_TRUE;
			break;
		case 'n':
			cb.cb_dryrun = B_TRUE;
			break;
		case 'd':
			cb.cb_defer_destroy = B_TRUE;
			type = ZFS_TYPE_SNAPSHOT;
			break;
		case 'f':
			cb.cb_force = B_TRUE;
			break;
		case 'r':
			cb.cb_recurse = B_TRUE;
			break;
		case 'R':
			cb.cb_recurse = B_TRUE;
			cb.cb_doclones = B_TRUE;
			break;
		case '?':
		default:
			(void) fprintf(stderr, gettext("invalid option '%c'\n"),
			    optopt);
			usage(B_FALSE);
		}
	}

	argc -= optind;
	argv += optind;

	/* check number of arguments */
	if (argc == 0) {
		(void) fprintf(stderr, gettext("missing dataset argument\n"));
		usage(B_FALSE);
	}
	if (argc > 1) {
		(void) fprintf(stderr, gettext("too many arguments\n"));
		usage(B_FALSE);
	}

	at = strchr(argv[0], '@');
	pound = strchr(argv[0], '#');
	if (at != NULL) {

		/* Build the list of snaps to destroy in cb_nvl. */
		cb.cb_nvl = fnvlist_alloc();

		*at = '\0';
		zhp = zfs_open(g_zfs, argv[0],
		    ZFS_TYPE_FILESYSTEM | ZFS_TYPE_VOLUME);
		if (zhp == NULL) {
			nvlist_free(cb.cb_nvl);
			return (1);
		}

		cb.cb_snapspec = at + 1;
		if (gather_snapshots(zfs_handle_dup(zhp), &cb) != 0 ||
		    cb.cb_error) {
			rv = 1;
			goto out;
		}

		if (nvlist_empty(cb.cb_nvl)) {
			(void) fprintf(stderr, gettext("could not find any "
			    "snapshots to destroy; check snapshot names.\n"));
			rv = 1;
			goto out;
		}

		if (cb.cb_verbose) {
			char buf[16];
			zfs_nicebytes(cb.cb_snapused, buf, sizeof (buf));
			if (cb.cb_parsable) {
				(void) printf("reclaim\t%llu\n",
				    (u_longlong_t)cb.cb_snapused);
			} else if (cb.cb_dryrun) {
				(void) printf(gettext("would reclaim %s\n"),
				    buf);
			} else {
				(void) printf(gettext("will reclaim %s\n"),
				    buf);
			}
		}

		if (!cb.cb_dryrun) {
			if (cb.cb_doclones) {
				cb.cb_batchedsnaps = fnvlist_alloc();
				err = destroy_clones(&cb);
				if (err == 0) {
					err = zfs_destroy_snaps_nvl(g_zfs,
					    cb.cb_batchedsnaps, B_FALSE);
				}
				if (err != 0) {
					rv = 1;
					goto out;
				}
			}
			if (err == 0) {
				err = zfs_destroy_snaps_nvl(g_zfs, cb.cb_nvl,
				    cb.cb_defer_destroy);
			}
		}

		if (err != 0)
			rv = 1;
	} else if (pound != NULL) {
		int err;
		nvlist_t *nvl;

		if (cb.cb_dryrun) {
			(void) fprintf(stderr,
			    "dryrun is not supported with bookmark\n");
			return (-1);
		}

		if (cb.cb_defer_destroy) {
			(void) fprintf(stderr,
			    "defer destroy is not supported with bookmark\n");
			return (-1);
		}

		if (cb.cb_recurse) {
			(void) fprintf(stderr,
			    "recursive is not supported with bookmark\n");
			return (-1);
		}

		/*
		 * Unfortunately, zfs_bookmark() doesn't honor the
		 * casesensitivity setting.  However, we can't simply
		 * remove this check, because lzc_destroy_bookmarks()
		 * ignores non-existent bookmarks, so this is necessary
		 * to get a proper error message.
		 */
		if (!zfs_bookmark_exists(argv[0])) {
			(void) fprintf(stderr, gettext("bookmark '%s' "
			    "does not exist.\n"), argv[0]);
			return (1);
		}

		nvl = fnvlist_alloc();
		fnvlist_add_boolean(nvl, argv[0]);

		err = lzc_destroy_bookmarks(nvl, NULL);
		if (err != 0) {
			(void) zfs_standard_error(g_zfs, err,
			    "cannot destroy bookmark");
		}

		nvlist_free(nvl);

		return (err);
	} else {
		/* Open the given dataset */
		if ((zhp = zfs_open(g_zfs, argv[0], type)) == NULL)
			return (1);

		cb.cb_target = zhp;

		/*
		 * Perform an explicit check for pools before going any further.
		 */
		if (!cb.cb_recurse && strchr(zfs_get_name(zhp), '/') == NULL &&
		    zfs_get_type(zhp) == ZFS_TYPE_FILESYSTEM) {
			(void) fprintf(stderr, gettext("cannot destroy '%s': "
			    "operation does not apply to pools\n"),
			    zfs_get_name(zhp));
			(void) fprintf(stderr, gettext("use 'zfs destroy -r "
			    "%s' to destroy all datasets in the pool\n"),
			    zfs_get_name(zhp));
			(void) fprintf(stderr, gettext("use 'zpool destroy %s' "
			    "to destroy the pool itself\n"), zfs_get_name(zhp));
			rv = 1;
			goto out;
		}

		/*
		 * Check for any dependents and/or clones.
		 */
		cb.cb_first = B_TRUE;
		if (!cb.cb_doclones &&
		    zfs_iter_dependents(zhp, B_TRUE, destroy_check_dependent,
		    &cb) != 0) {
			rv = 1;
			goto out;
		}

		if (cb.cb_error) {
			rv = 1;
			goto out;
		}
		cb.cb_batchedsnaps = fnvlist_alloc();
		if (zfs_iter_dependents(zhp, B_FALSE, destroy_callback,
		    &cb) != 0) {
			rv = 1;
			goto out;
		}

		/*
		 * Do the real thing.  The callback will close the
		 * handle regardless of whether it succeeds or not.
		 */
		err = destroy_callback(zhp, &cb);
		zhp = NULL;
		if (err == 0) {
			err = zfs_destroy_snaps_nvl(g_zfs,
			    cb.cb_batchedsnaps, cb.cb_defer_destroy);
		}
		if (err != 0 || cb.cb_error == B_TRUE)
			rv = 1;
	}

out:
	fnvlist_free(cb.cb_batchedsnaps);
	fnvlist_free(cb.cb_nvl);
	if (zhp != NULL)
		zfs_close(zhp);
	return (rv);
}

static boolean_t
is_recvd_column(zprop_get_cbdata_t *cbp)
{
	int i;
	zfs_get_column_t col;

	for (i = 0; i < ZFS_GET_NCOLS &&
	    (col = cbp->cb_columns[i]) != GET_COL_NONE; i++)
		if (col == GET_COL_RECVD)
			return (B_TRUE);
	return (B_FALSE);
}

/*
 * zfs get [-rHp] [-o all | field[,field]...] [-s source[,source]...]
 *	< all | property[,property]... > < fs | snap | vol > ...
 *
 *	-r	recurse over any child datasets
 *	-H	scripted mode.  Headers are stripped, and fields are separated
 *		by tabs instead of spaces.
 *	-o	Set of fields to display.  One of "name,property,value,
 *		received,source". Default is "name,property,value,source".
 *		"all" is an alias for all five.
 *	-s	Set of sources to allow.  One of
 *		"local,default,inherited,received,temporary,none".  Default is
 *		all six.
 *	-p	Display values in parsable (literal) format.
 *
 *  Prints properties for the given datasets.  The user can control which
 *  columns to display as well as which property types to allow.
 */

/*
 * Invoked to display the properties for a single dataset.
 */
static int
get_callback(zfs_handle_t *zhp, void *data)
{
	char buf[ZFS_MAXPROPLEN];
	char rbuf[ZFS_MAXPROPLEN];
	zprop_source_t sourcetype;
	char source[ZFS_MAX_DATASET_NAME_LEN];
	zprop_get_cbdata_t *cbp = data;
	nvlist_t *user_props = zfs_get_user_props(zhp);
	zprop_list_t *pl = cbp->cb_proplist;
	nvlist_t *propval;
	char *strval;
	char *sourceval;
	boolean_t received = is_recvd_column(cbp);

	for (; pl != NULL; pl = pl->pl_next) {
		char *recvdval = NULL;
		/*
		 * Skip the special fake placeholder.  This will also skip over
		 * the name property when 'all' is specified.
		 */
		if (pl->pl_prop == ZFS_PROP_NAME &&
		    pl == cbp->cb_proplist)
			continue;

		if (pl->pl_prop != ZPROP_INVAL) {
			if (zfs_prop_get(zhp, pl->pl_prop, buf,
			    sizeof (buf), &sourcetype, source,
			    sizeof (source),
			    cbp->cb_literal) != 0) {
				if (pl->pl_all)
					continue;
				if (!zfs_prop_valid_for_type(pl->pl_prop,
				    ZFS_TYPE_DATASET, B_FALSE)) {
					(void) fprintf(stderr,
					    gettext("No such property '%s'\n"),
					    zfs_prop_to_name(pl->pl_prop));
					continue;
				}
				sourcetype = ZPROP_SRC_NONE;
				(void) strlcpy(buf, "-", sizeof (buf));
			}

			if (received && (zfs_prop_get_recvd(zhp,
			    zfs_prop_to_name(pl->pl_prop), rbuf, sizeof (rbuf),
			    cbp->cb_literal) == 0))
				recvdval = rbuf;

			zprop_print_one_property(zfs_get_name(zhp), cbp,
			    zfs_prop_to_name(pl->pl_prop),
			    buf, sourcetype, source, recvdval);
		} else if (zfs_prop_userquota(pl->pl_user_prop)) {
			sourcetype = ZPROP_SRC_LOCAL;

			if (zfs_prop_get_userquota(zhp, pl->pl_user_prop,
			    buf, sizeof (buf), cbp->cb_literal) != 0) {
				sourcetype = ZPROP_SRC_NONE;
				(void) strlcpy(buf, "-", sizeof (buf));
			}

			zprop_print_one_property(zfs_get_name(zhp), cbp,
			    pl->pl_user_prop, buf, sourcetype, source, NULL);
		} else if (zfs_prop_written(pl->pl_user_prop)) {
			sourcetype = ZPROP_SRC_LOCAL;

			if (zfs_prop_get_written(zhp, pl->pl_user_prop,
			    buf, sizeof (buf), cbp->cb_literal) != 0) {
				sourcetype = ZPROP_SRC_NONE;
				(void) strlcpy(buf, "-", sizeof (buf));
			}

			zprop_print_one_property(zfs_get_name(zhp), cbp,
			    pl->pl_user_prop, buf, sourcetype, source, NULL);
		} else {
			if (nvlist_lookup_nvlist(user_props,
			    pl->pl_user_prop, &propval) != 0) {
				if (pl->pl_all)
					continue;
				sourcetype = ZPROP_SRC_NONE;
				strval = "-";
			} else {
				verify(nvlist_lookup_string(propval,
				    ZPROP_VALUE, &strval) == 0);
				verify(nvlist_lookup_string(propval,
				    ZPROP_SOURCE, &sourceval) == 0);

				if (strcmp(sourceval,
				    zfs_get_name(zhp)) == 0) {
					sourcetype = ZPROP_SRC_LOCAL;
				} else if (strcmp(sourceval,
				    ZPROP_SOURCE_VAL_RECVD) == 0) {
					sourcetype = ZPROP_SRC_RECEIVED;
				} else {
					sourcetype = ZPROP_SRC_INHERITED;
					(void) strlcpy(source,
					    sourceval, sizeof (source));
				}
			}

			if (received && (zfs_prop_get_recvd(zhp,
			    pl->pl_user_prop, rbuf, sizeof (rbuf),
			    cbp->cb_literal) == 0))
				recvdval = rbuf;

			zprop_print_one_property(zfs_get_name(zhp), cbp,
			    pl->pl_user_prop, strval, sourcetype,
			    source, recvdval);
		}
	}

	return (0);
}

static int
zfs_do_get(int argc, char **argv)
{
	zprop_get_cbdata_t cb = { 0 };
	int i, c, flags = ZFS_ITER_ARGS_CAN_BE_PATHS;
	int types = ZFS_TYPE_DATASET | ZFS_TYPE_BOOKMARK;
	char *value, *fields;
	int ret = 0;
	int limit = 0;
	zprop_list_t fake_name = { 0 };

	/*
	 * Set up default columns and sources.
	 */
	cb.cb_sources = ZPROP_SRC_ALL;
	cb.cb_columns[0] = GET_COL_NAME;
	cb.cb_columns[1] = GET_COL_PROPERTY;
	cb.cb_columns[2] = GET_COL_VALUE;
	cb.cb_columns[3] = GET_COL_SOURCE;
	cb.cb_type = ZFS_TYPE_DATASET;

	/* check options */
	while ((c = getopt(argc, argv, ":d:o:s:rt:Hp")) != -1) {
		switch (c) {
		case 'p':
			cb.cb_literal = B_TRUE;
			break;
		case 'd':
			limit = parse_depth(optarg, &flags);
			break;
		case 'r':
			flags |= ZFS_ITER_RECURSE;
			break;
		case 'H':
			cb.cb_scripted = B_TRUE;
			break;
		case ':':
			(void) fprintf(stderr, gettext("missing argument for "
			    "'%c' option\n"), optopt);
			usage(B_FALSE);
			break;
		case 'o':
			/*
			 * Process the set of columns to display.  We zero out
			 * the structure to give us a blank slate.
			 */
			bzero(&cb.cb_columns, sizeof (cb.cb_columns));
			i = 0;
			while (*optarg != '\0') {
				static char *col_subopts[] =
				    { "name", "property", "value", "received",
				    "source", "all", NULL };

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
					cb.cb_columns[i++] = GET_COL_RECVD;
					flags |= ZFS_ITER_RECVD_PROPS;
					break;
				case 4:
					cb.cb_columns[i++] = GET_COL_SOURCE;
					break;
				case 5:
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
					cb.cb_columns[3] = GET_COL_RECVD;
					cb.cb_columns[4] = GET_COL_SOURCE;
					flags |= ZFS_ITER_RECVD_PROPS;
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

		case 's':
			cb.cb_sources = 0;
			while (*optarg != '\0') {
				static char *source_subopts[] = {
					"local", "default", "inherited",
					"received", "temporary", "none",
					NULL };

				switch (getsubopt(&optarg, source_subopts,
				    &value)) {
				case 0:
					cb.cb_sources |= ZPROP_SRC_LOCAL;
					break;
				case 1:
					cb.cb_sources |= ZPROP_SRC_DEFAULT;
					break;
				case 2:
					cb.cb_sources |= ZPROP_SRC_INHERITED;
					break;
				case 3:
					cb.cb_sources |= ZPROP_SRC_RECEIVED;
					break;
				case 4:
					cb.cb_sources |= ZPROP_SRC_TEMPORARY;
					break;
				case 5:
					cb.cb_sources |= ZPROP_SRC_NONE;
					break;
				default:
					(void) fprintf(stderr,
					    gettext("invalid source "
					    "'%s'\n"), value);
					usage(B_FALSE);
				}
			}
			break;

		case 't':
			types = 0;
			flags &= ~ZFS_ITER_PROP_LISTSNAPS;
			while (*optarg != '\0') {
				static char *type_subopts[] = { "filesystem",
				    "volume", "snapshot", "snap", "bookmark",
				    "all", NULL };

				switch (getsubopt(&optarg, type_subopts,
				    &value)) {
				case 0:
					types |= ZFS_TYPE_FILESYSTEM;
					break;
				case 1:
					types |= ZFS_TYPE_VOLUME;
					break;
				case 2:
				case 3:
					types |= ZFS_TYPE_SNAPSHOT;
					break;
				case 4:
					types |= ZFS_TYPE_BOOKMARK;
					break;
				case 5:
					types = ZFS_TYPE_DATASET |
					    ZFS_TYPE_BOOKMARK;
					break;

				default:
					(void) fprintf(stderr,
					    gettext("invalid type '%s'\n"),
					    value);
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

	fields = argv[0];

	/*
	 * Handle users who want to get all snapshots or bookmarks
	 * of a dataset (ex. 'zfs get -t snapshot refer <dataset>').
	 */
	if ((types == ZFS_TYPE_SNAPSHOT || types == ZFS_TYPE_BOOKMARK) &&
	    argc > 1 && (flags & ZFS_ITER_RECURSE) == 0 && limit == 0) {
		flags |= (ZFS_ITER_DEPTH_LIMIT | ZFS_ITER_RECURSE);
		limit = 1;
	}

	if (zprop_get_list(g_zfs, fields, &cb.cb_proplist, ZFS_TYPE_DATASET)
	    != 0)
		usage(B_FALSE);

	argc--;
	argv++;

	/*
	 * As part of zfs_expand_proplist(), we keep track of the maximum column
	 * width for each property.  For the 'NAME' (and 'SOURCE') columns, we
	 * need to know the maximum name length.  However, the user likely did
	 * not specify 'name' as one of the properties to fetch, so we need to
	 * make sure we always include at least this property for
	 * print_get_headers() to work properly.
	 */
	if (cb.cb_proplist != NULL) {
		fake_name.pl_prop = ZFS_PROP_NAME;
		fake_name.pl_width = strlen(gettext("NAME"));
		fake_name.pl_next = cb.cb_proplist;
		cb.cb_proplist = &fake_name;
	}

	cb.cb_first = B_TRUE;

	/* run for each object */
	ret = zfs_for_each(argc, argv, flags, types, NULL,
	    &cb.cb_proplist, limit, get_callback, &cb);

	if (cb.cb_proplist == &fake_name)
		zprop_free_list(fake_name.pl_next);
	else
		zprop_free_list(cb.cb_proplist);

	return (ret);
}

/*
 * inherit [-rS] <property> <fs|vol> ...
 *
 *	-r	Recurse over all children
 *	-S	Revert to received value, if any
 *
 * For each dataset specified on the command line, inherit the given property
 * from its parent.  Inheriting a property at the pool level will cause it to
 * use the default value.  The '-r' flag will recurse over all children, and is
 * useful for setting a property on a hierarchy-wide basis, regardless of any
 * local modifications for each dataset.
 */

typedef struct inherit_cbdata {
	const char *cb_propname;
	boolean_t cb_received;
} inherit_cbdata_t;

static int
inherit_recurse_cb(zfs_handle_t *zhp, void *data)
{
	inherit_cbdata_t *cb = data;
	zfs_prop_t prop = zfs_name_to_prop(cb->cb_propname);

	/*
	 * If we're doing it recursively, then ignore properties that
	 * are not valid for this type of dataset.
	 */
	if (prop != ZPROP_INVAL &&
	    !zfs_prop_valid_for_type(prop, zfs_get_type(zhp), B_FALSE))
		return (0);

	return (zfs_prop_inherit(zhp, cb->cb_propname, cb->cb_received) != 0);
}

static int
inherit_cb(zfs_handle_t *zhp, void *data)
{
	inherit_cbdata_t *cb = data;

	return (zfs_prop_inherit(zhp, cb->cb_propname, cb->cb_received) != 0);
}

static int
zfs_do_inherit(int argc, char **argv)
{
	int c;
	zfs_prop_t prop;
	inherit_cbdata_t cb = { 0 };
	char *propname;
	int ret = 0;
	int flags = 0;
	boolean_t received = B_FALSE;

	/* check options */
	while ((c = getopt(argc, argv, "rS")) != -1) {
		switch (c) {
		case 'r':
			flags |= ZFS_ITER_RECURSE;
			break;
		case 'S':
			received = B_TRUE;
			break;
		case '?':
		default:
			(void) fprintf(stderr, gettext("invalid option '%c'\n"),
			    optopt);
			usage(B_FALSE);
		}
	}

	argc -= optind;
	argv += optind;

	/* check number of arguments */
	if (argc < 1) {
		(void) fprintf(stderr, gettext("missing property argument\n"));
		usage(B_FALSE);
	}
	if (argc < 2) {
		(void) fprintf(stderr, gettext("missing dataset argument\n"));
		usage(B_FALSE);
	}

	propname = argv[0];
	argc--;
	argv++;

	if ((prop = zfs_name_to_prop(propname)) != ZPROP_INVAL) {
		if (zfs_prop_readonly(prop)) {
			(void) fprintf(stderr, gettext(
			    "%s property is read-only\n"),
			    propname);
			return (1);
		}
		if (!zfs_prop_inheritable(prop) && !received) {
			(void) fprintf(stderr, gettext("'%s' property cannot "
			    "be inherited\n"), propname);
			if (prop == ZFS_PROP_QUOTA ||
			    prop == ZFS_PROP_RESERVATION ||
			    prop == ZFS_PROP_REFQUOTA ||
			    prop == ZFS_PROP_REFRESERVATION) {
				(void) fprintf(stderr, gettext("use 'zfs set "
				    "%s=none' to clear\n"), propname);
				(void) fprintf(stderr, gettext("use 'zfs "
				    "inherit -S %s' to revert to received "
				    "value\n"), propname);
			}
			return (1);
		}
		if (received && (prop == ZFS_PROP_VOLSIZE ||
		    prop == ZFS_PROP_VERSION)) {
			(void) fprintf(stderr, gettext("'%s' property cannot "
			    "be reverted to a received value\n"), propname);
			return (1);
		}
	} else if (!zfs_prop_user(propname)) {
		(void) fprintf(stderr, gettext("invalid property '%s'\n"),
		    propname);
		usage(B_FALSE);
	}

	cb.cb_propname = propname;
	cb.cb_received = received;

	if (flags & ZFS_ITER_RECURSE) {
		ret = zfs_for_each(argc, argv, flags, ZFS_TYPE_DATASET,
		    NULL, NULL, 0, inherit_recurse_cb, &cb);
	} else {
		ret = zfs_for_each(argc, argv, flags, ZFS_TYPE_DATASET,
		    NULL, NULL, 0, inherit_cb, &cb);
	}

	return (ret);
}

typedef struct upgrade_cbdata {
	uint64_t cb_numupgraded;
	uint64_t cb_numsamegraded;
	uint64_t cb_numfailed;
	uint64_t cb_version;
	boolean_t cb_newer;
	boolean_t cb_foundone;
	char cb_lastfs[ZFS_MAX_DATASET_NAME_LEN];
} upgrade_cbdata_t;

static int
same_pool(zfs_handle_t *zhp, const char *name)
{
	int len1 = strcspn(name, "/@");
	const char *zhname = zfs_get_name(zhp);
	int len2 = strcspn(zhname, "/@");

	if (len1 != len2)
		return (B_FALSE);
	return (strncmp(name, zhname, len1) == 0);
}

static int
upgrade_list_callback(zfs_handle_t *zhp, void *data)
{
	upgrade_cbdata_t *cb = data;
	int version = zfs_prop_get_int(zhp, ZFS_PROP_VERSION);

	/* list if it's old/new */
	if ((!cb->cb_newer && version < ZPL_VERSION) ||
	    (cb->cb_newer && version > ZPL_VERSION)) {
		char *str;
		if (cb->cb_newer) {
			str = gettext("The following filesystems are "
			    "formatted using a newer software version and\n"
			    "cannot be accessed on the current system.\n\n");
		} else {
			str = gettext("The following filesystems are "
			    "out of date, and can be upgraded.  After being\n"
			    "upgraded, these filesystems (and any 'zfs send' "
			    "streams generated from\n"
			    "subsequent snapshots) will no longer be "
			    "accessible by older software versions.\n\n");
		}

		if (!cb->cb_foundone) {
			(void) puts(str);
			(void) printf(gettext("VER  FILESYSTEM\n"));
			(void) printf(gettext("---  ------------\n"));
			cb->cb_foundone = B_TRUE;
		}

		(void) printf("%2u   %s\n", version, zfs_get_name(zhp));
	}

	return (0);
}

static int
upgrade_set_callback(zfs_handle_t *zhp, void *data)
{
	upgrade_cbdata_t *cb = data;
	int version = zfs_prop_get_int(zhp, ZFS_PROP_VERSION);
	int needed_spa_version;
	int spa_version;

	if (zfs_spa_version(zhp, &spa_version) < 0)
		return (-1);

	needed_spa_version = zfs_spa_version_map(cb->cb_version);

	if (needed_spa_version < 0)
		return (-1);

	if (spa_version < needed_spa_version) {
		/* can't upgrade */
		(void) printf(gettext("%s: can not be "
		    "upgraded; the pool version needs to first "
		    "be upgraded\nto version %d\n\n"),
		    zfs_get_name(zhp), needed_spa_version);
		cb->cb_numfailed++;
		return (0);
	}

	/* upgrade */
	if (version < cb->cb_version) {
		char verstr[16];
		(void) snprintf(verstr, sizeof (verstr),
		    "%llu", (u_longlong_t)cb->cb_version);
		if (cb->cb_lastfs[0] && !same_pool(zhp, cb->cb_lastfs)) {
			/*
			 * If they did "zfs upgrade -a", then we could
			 * be doing ioctls to different pools.  We need
			 * to log this history once to each pool, and bypass
			 * the normal history logging that happens in main().
			 */
			(void) zpool_log_history(g_zfs, history_str);
			log_history = B_FALSE;
		}
		if (zfs_prop_set(zhp, "version", verstr) == 0)
			cb->cb_numupgraded++;
		else
			cb->cb_numfailed++;
		(void) strcpy(cb->cb_lastfs, zfs_get_name(zhp));
	} else if (version > cb->cb_version) {
		/* can't downgrade */
		(void) printf(gettext("%s: can not be downgraded; "
		    "it is already at version %u\n"),
		    zfs_get_name(zhp), version);
		cb->cb_numfailed++;
	} else {
		cb->cb_numsamegraded++;
	}
	return (0);
}

/*
 * zfs upgrade
 * zfs upgrade -v
 * zfs upgrade [-r] [-V <version>] <-a | filesystem>
 */
static int
zfs_do_upgrade(int argc, char **argv)
{
	boolean_t all = B_FALSE;
	boolean_t showversions = B_FALSE;
	int ret = 0;
	upgrade_cbdata_t cb = { 0 };
	int c;
	int flags = ZFS_ITER_ARGS_CAN_BE_PATHS;

	/* check options */
	while ((c = getopt(argc, argv, "rvV:a")) != -1) {
		switch (c) {
		case 'r':
			flags |= ZFS_ITER_RECURSE;
			break;
		case 'v':
			showversions = B_TRUE;
			break;
		case 'V':
			if (zfs_prop_string_to_index(ZFS_PROP_VERSION,
			    optarg, &cb.cb_version) != 0) {
				(void) fprintf(stderr,
				    gettext("invalid version %s\n"), optarg);
				usage(B_FALSE);
			}
			break;
		case 'a':
			all = B_TRUE;
			break;
		case '?':
		default:
			(void) fprintf(stderr, gettext("invalid option '%c'\n"),
			    optopt);
			usage(B_FALSE);
		}
	}

	argc -= optind;
	argv += optind;

	if ((!all && !argc) && ((flags & ZFS_ITER_RECURSE) | cb.cb_version))
		usage(B_FALSE);
	if (showversions && (flags & ZFS_ITER_RECURSE || all ||
	    cb.cb_version || argc))
		usage(B_FALSE);
	if ((all || argc) && (showversions))
		usage(B_FALSE);
	if (all && argc)
		usage(B_FALSE);

	if (showversions) {
		/* Show info on available versions. */
		(void) printf(gettext("The following filesystem versions are "
		    "supported:\n\n"));
		(void) printf(gettext("VER  DESCRIPTION\n"));
		(void) printf("---  -----------------------------------------"
		    "---------------\n");
		(void) printf(gettext(" 1   Initial ZFS filesystem version\n"));
		(void) printf(gettext(" 2   Enhanced directory entries\n"));
		(void) printf(gettext(" 3   Case insensitive and filesystem "
		    "user identifier (FUID)\n"));
		(void) printf(gettext(" 4   userquota, groupquota "
		    "properties\n"));
		(void) printf(gettext(" 5   System attributes\n"));
		(void) printf(gettext("\nFor more information on a particular "
		    "version, including supported releases,\n"));
		(void) printf("see the ZFS Administration Guide.\n\n");
		ret = 0;
	} else if (argc || all) {
		/* Upgrade filesystems */
		if (cb.cb_version == 0)
			cb.cb_version = ZPL_VERSION;
		ret = zfs_for_each(argc, argv, flags, ZFS_TYPE_FILESYSTEM,
		    NULL, NULL, 0, upgrade_set_callback, &cb);
		(void) printf(gettext("%llu filesystems upgraded\n"),
		    (u_longlong_t)cb.cb_numupgraded);
		if (cb.cb_numsamegraded) {
			(void) printf(gettext("%llu filesystems already at "
			    "this version\n"),
			    (u_longlong_t)cb.cb_numsamegraded);
		}
		if (cb.cb_numfailed != 0)
			ret = 1;
	} else {
		/* List old-version filesystems */
		boolean_t found;
		(void) printf(gettext("This system is currently running "
		    "ZFS filesystem version %llu.\n\n"), ZPL_VERSION);

		flags |= ZFS_ITER_RECURSE;
		ret = zfs_for_each(0, NULL, flags, ZFS_TYPE_FILESYSTEM,
		    NULL, NULL, 0, upgrade_list_callback, &cb);

		found = cb.cb_foundone;
		cb.cb_foundone = B_FALSE;
		cb.cb_newer = B_TRUE;

		ret = zfs_for_each(0, NULL, flags, ZFS_TYPE_FILESYSTEM,
		    NULL, NULL, 0, upgrade_list_callback, &cb);

		if (!cb.cb_foundone && !found) {
			(void) printf(gettext("All filesystems are "
			    "formatted with the current version.\n"));
		}
	}

	return (ret);
}

/*
 * zfs userspace [-Hinp] [-o field[,...]] [-s field [-s field]...]
 *               [-S field [-S field]...] [-t type[,...]]
 *               filesystem | snapshot | path
 * zfs groupspace [-Hinp] [-o field[,...]] [-s field [-s field]...]
 *                [-S field [-S field]...] [-t type[,...]]
 *                filesystem | snapshot | path
 * zfs projectspace [-Hp] [-o field[,...]] [-s field [-s field]...]
 *                [-S field [-S field]...] filesystem | snapshot | path
 *
 *	-H      Scripted mode; elide headers and separate columns by tabs.
 *	-i	Translate SID to POSIX ID.
 *	-n	Print numeric ID instead of user/group name.
 *	-o      Control which fields to display.
 *	-p	Use exact (parsable) numeric output.
 *	-s      Specify sort columns, descending order.
 *	-S      Specify sort columns, ascending order.
 *	-t      Control which object types to display.
 *
 *	Displays space consumed by, and quotas on, each user in the specified
 *	filesystem or snapshot.
 */

/* us_field_types, us_field_hdr and us_field_names should be kept in sync */
enum us_field_types {
	USFIELD_TYPE,
	USFIELD_NAME,
	USFIELD_USED,
	USFIELD_QUOTA,
	USFIELD_OBJUSED,
	USFIELD_OBJQUOTA
};
static char *us_field_hdr[] = { "TYPE", "NAME", "USED", "QUOTA",
				    "OBJUSED", "OBJQUOTA" };
static char *us_field_names[] = { "type", "name", "used", "quota",
				    "objused", "objquota" };
#define	USFIELD_LAST	(sizeof (us_field_names) / sizeof (char *))

#define	USTYPE_PSX_GRP	(1 << 0)
#define	USTYPE_PSX_USR	(1 << 1)
#define	USTYPE_SMB_GRP	(1 << 2)
#define	USTYPE_SMB_USR	(1 << 3)
#define	USTYPE_PROJ	(1 << 4)
#define	USTYPE_ALL	\
	(USTYPE_PSX_GRP | USTYPE_PSX_USR | USTYPE_SMB_GRP | USTYPE_SMB_USR | \
	    USTYPE_PROJ)

static int us_type_bits[] = {
	USTYPE_PSX_GRP,
	USTYPE_PSX_USR,
	USTYPE_SMB_GRP,
	USTYPE_SMB_USR,
	USTYPE_ALL
};
static char *us_type_names[] = { "posixgroup", "posixuser", "smbgroup",
	"smbuser", "all" };

typedef struct us_node {
	nvlist_t	*usn_nvl;
	uu_avl_node_t	usn_avlnode;
	uu_list_node_t	usn_listnode;
} us_node_t;

typedef struct us_cbdata {
	nvlist_t	**cb_nvlp;
	uu_avl_pool_t	*cb_avl_pool;
	uu_avl_t	*cb_avl;
	boolean_t	cb_numname;
	boolean_t	cb_nicenum;
	boolean_t	cb_sid2posix;
	zfs_userquota_prop_t cb_prop;
	zfs_sort_column_t *cb_sortcol;
	size_t		cb_width[USFIELD_LAST];
} us_cbdata_t;

static boolean_t us_populated = B_FALSE;

typedef struct {
	zfs_sort_column_t *si_sortcol;
	boolean_t	si_numname;
} us_sort_info_t;

static int
us_field_index(char *field)
{
	int i;

	for (i = 0; i < USFIELD_LAST; i++) {
		if (strcmp(field, us_field_names[i]) == 0)
			return (i);
	}

	return (-1);
}

static int
us_compare(const void *larg, const void *rarg, void *unused)
{
	const us_node_t *l = larg;
	const us_node_t *r = rarg;
	us_sort_info_t *si = (us_sort_info_t *)unused;
	zfs_sort_column_t *sortcol = si->si_sortcol;
	boolean_t numname = si->si_numname;
	nvlist_t *lnvl = l->usn_nvl;
	nvlist_t *rnvl = r->usn_nvl;
	int rc = 0;
	boolean_t lvb, rvb;

	for (; sortcol != NULL; sortcol = sortcol->sc_next) {
		char *lvstr = "";
		char *rvstr = "";
		uint32_t lv32 = 0;
		uint32_t rv32 = 0;
		uint64_t lv64 = 0;
		uint64_t rv64 = 0;
		zfs_prop_t prop = sortcol->sc_prop;
		const char *propname = NULL;
		boolean_t reverse = sortcol->sc_reverse;

		switch (prop) {
		case ZFS_PROP_TYPE:
			propname = "type";
			(void) nvlist_lookup_uint32(lnvl, propname, &lv32);
			(void) nvlist_lookup_uint32(rnvl, propname, &rv32);
			if (rv32 != lv32)
				rc = (rv32 < lv32) ? 1 : -1;
			break;
		case ZFS_PROP_NAME:
			propname = "name";
			if (numname) {
compare_nums:
				(void) nvlist_lookup_uint64(lnvl, propname,
				    &lv64);
				(void) nvlist_lookup_uint64(rnvl, propname,
				    &rv64);
				if (rv64 != lv64)
					rc = (rv64 < lv64) ? 1 : -1;
			} else {
				if ((nvlist_lookup_string(lnvl, propname,
				    &lvstr) == ENOENT) ||
				    (nvlist_lookup_string(rnvl, propname,
				    &rvstr) == ENOENT)) {
					goto compare_nums;
				}
				rc = strcmp(lvstr, rvstr);
			}
			break;
		case ZFS_PROP_USED:
		case ZFS_PROP_QUOTA:
			if (!us_populated)
				break;
			if (prop == ZFS_PROP_USED)
				propname = "used";
			else
				propname = "quota";
			(void) nvlist_lookup_uint64(lnvl, propname, &lv64);
			(void) nvlist_lookup_uint64(rnvl, propname, &rv64);
			if (rv64 != lv64)
				rc = (rv64 < lv64) ? 1 : -1;
			break;

		default:
			break;
		}

		if (rc != 0) {
			if (rc < 0)
				return (reverse ? 1 : -1);
			else
				return (reverse ? -1 : 1);
		}
	}

	/*
	 * If entries still seem to be the same, check if they are of the same
	 * type (smbentity is added only if we are doing SID to POSIX ID
	 * translation where we can have duplicate type/name combinations).
	 */
	if (nvlist_lookup_boolean_value(lnvl, "smbentity", &lvb) == 0 &&
	    nvlist_lookup_boolean_value(rnvl, "smbentity", &rvb) == 0 &&
	    lvb != rvb)
		return (lvb < rvb ? -1 : 1);

	return (0);
}

static boolean_t
zfs_prop_is_user(unsigned p)
{
	return (p == ZFS_PROP_USERUSED || p == ZFS_PROP_USERQUOTA ||
	    p == ZFS_PROP_USEROBJUSED || p == ZFS_PROP_USEROBJQUOTA);
}

static boolean_t
zfs_prop_is_group(unsigned p)
{
	return (p == ZFS_PROP_GROUPUSED || p == ZFS_PROP_GROUPQUOTA ||
	    p == ZFS_PROP_GROUPOBJUSED || p == ZFS_PROP_GROUPOBJQUOTA);
}

static boolean_t
zfs_prop_is_project(unsigned p)
{
	return (p == ZFS_PROP_PROJECTUSED || p == ZFS_PROP_PROJECTQUOTA ||
	    p == ZFS_PROP_PROJECTOBJUSED || p == ZFS_PROP_PROJECTOBJQUOTA);
}

static inline const char *
us_type2str(unsigned field_type)
{
	switch (field_type) {
	case USTYPE_PSX_USR:
		return ("POSIX User");
	case USTYPE_PSX_GRP:
		return ("POSIX Group");
	case USTYPE_SMB_USR:
		return ("SMB User");
	case USTYPE_SMB_GRP:
		return ("SMB Group");
	case USTYPE_PROJ:
		return ("Project");
	default:
		return ("Undefined");
	}
}

static int
userspace_cb(void *arg, const char *domain, uid_t rid, uint64_t space)
{
	us_cbdata_t *cb = (us_cbdata_t *)arg;
	zfs_userquota_prop_t prop = cb->cb_prop;
	char *name = NULL;
	char *propname;
	char sizebuf[32];
	us_node_t *node;
	uu_avl_pool_t *avl_pool = cb->cb_avl_pool;
	uu_avl_t *avl = cb->cb_avl;
	uu_avl_index_t idx;
	nvlist_t *props;
	us_node_t *n;
	zfs_sort_column_t *sortcol = cb->cb_sortcol;
	unsigned type = 0;
	const char *typestr;
	size_t namelen;
	size_t typelen;
	size_t sizelen;
	int typeidx, nameidx, sizeidx;
	us_sort_info_t sortinfo = { sortcol, cb->cb_numname };
	boolean_t smbentity = B_FALSE;

	if (nvlist_alloc(&props, NV_UNIQUE_NAME, 0) != 0)
		nomem();
	node = safe_malloc(sizeof (us_node_t));
	uu_avl_node_init(node, &node->usn_avlnode, avl_pool);
	node->usn_nvl = props;

	if (domain != NULL && domain[0] != '\0') {
#ifdef HAVE_IDMAP
		/* SMB */
		char sid[MAXNAMELEN + 32];
		uid_t id;
		uint64_t classes;
		int err;
		directory_error_t e;

		smbentity = B_TRUE;

		(void) snprintf(sid, sizeof (sid), "%s-%u", domain, rid);

		if (prop == ZFS_PROP_GROUPUSED || prop == ZFS_PROP_GROUPQUOTA) {
			type = USTYPE_SMB_GRP;
			err = sid_to_id(sid, B_FALSE, &id);
		} else {
			type = USTYPE_SMB_USR;
			err = sid_to_id(sid, B_TRUE, &id);
		}

		if (err == 0) {
			rid = id;
			if (!cb->cb_sid2posix) {
				e = directory_name_from_sid(NULL, sid, &name,
				    &classes);
				if (e != NULL)
					directory_error_free(e);
				if (name == NULL)
					name = sid;
			}
		}
#else
		nvlist_free(props);
		free(node);

		return (-1);
#endif /* HAVE_IDMAP */
	}

	if (cb->cb_sid2posix || domain == NULL || domain[0] == '\0') {
		/* POSIX or -i */
		if (zfs_prop_is_group(prop)) {
			type = USTYPE_PSX_GRP;
			if (!cb->cb_numname) {
				struct group *g;

				if ((g = getgrgid(rid)) != NULL)
					name = g->gr_name;
			}
		} else if (zfs_prop_is_user(prop)) {
			type = USTYPE_PSX_USR;
			if (!cb->cb_numname) {
				struct passwd *p;

				if ((p = getpwuid(rid)) != NULL)
					name = p->pw_name;
			}
		} else {
			type = USTYPE_PROJ;
		}
	}

	/*
	 * Make sure that the type/name combination is unique when doing
	 * SID to POSIX ID translation (hence changing the type from SMB to
	 * POSIX).
	 */
	if (cb->cb_sid2posix &&
	    nvlist_add_boolean_value(props, "smbentity", smbentity) != 0)
		nomem();

	/* Calculate/update width of TYPE field */
	typestr = us_type2str(type);
	typelen = strlen(gettext(typestr));
	typeidx = us_field_index("type");
	if (typelen > cb->cb_width[typeidx])
		cb->cb_width[typeidx] = typelen;
	if (nvlist_add_uint32(props, "type", type) != 0)
		nomem();

	/* Calculate/update width of NAME field */
	if ((cb->cb_numname && cb->cb_sid2posix) || name == NULL) {
		if (nvlist_add_uint64(props, "name", rid) != 0)
			nomem();
		namelen = snprintf(NULL, 0, "%u", rid);
	} else {
		if (nvlist_add_string(props, "name", name) != 0)
			nomem();
		namelen = strlen(name);
	}
	nameidx = us_field_index("name");
	if (nameidx >= 0 && namelen > cb->cb_width[nameidx])
		cb->cb_width[nameidx] = namelen;

	/*
	 * Check if this type/name combination is in the list and update it;
	 * otherwise add new node to the list.
	 */
	if ((n = uu_avl_find(avl, node, &sortinfo, &idx)) == NULL) {
		uu_avl_insert(avl, node, idx);
	} else {
		nvlist_free(props);
		free(node);
		node = n;
		props = node->usn_nvl;
	}

	/* Calculate/update width of USED/QUOTA fields */
	if (cb->cb_nicenum) {
		if (prop == ZFS_PROP_USERUSED || prop == ZFS_PROP_GROUPUSED ||
		    prop == ZFS_PROP_USERQUOTA || prop == ZFS_PROP_GROUPQUOTA ||
		    prop == ZFS_PROP_PROJECTUSED ||
		    prop == ZFS_PROP_PROJECTQUOTA) {
			zfs_nicebytes(space, sizebuf, sizeof (sizebuf));
		} else {
			zfs_nicenum(space, sizebuf, sizeof (sizebuf));
		}
	} else {
		(void) snprintf(sizebuf, sizeof (sizebuf), "%llu",
		    (u_longlong_t)space);
	}
	sizelen = strlen(sizebuf);
	if (prop == ZFS_PROP_USERUSED || prop == ZFS_PROP_GROUPUSED ||
	    prop == ZFS_PROP_PROJECTUSED) {
		propname = "used";
		if (!nvlist_exists(props, "quota"))
			(void) nvlist_add_uint64(props, "quota", 0);
	} else if (prop == ZFS_PROP_USERQUOTA || prop == ZFS_PROP_GROUPQUOTA ||
	    prop == ZFS_PROP_PROJECTQUOTA) {
		propname = "quota";
		if (!nvlist_exists(props, "used"))
			(void) nvlist_add_uint64(props, "used", 0);
	} else if (prop == ZFS_PROP_USEROBJUSED ||
	    prop == ZFS_PROP_GROUPOBJUSED || prop == ZFS_PROP_PROJECTOBJUSED) {
		propname = "objused";
		if (!nvlist_exists(props, "objquota"))
			(void) nvlist_add_uint64(props, "objquota", 0);
	} else if (prop == ZFS_PROP_USEROBJQUOTA ||
	    prop == ZFS_PROP_GROUPOBJQUOTA ||
	    prop == ZFS_PROP_PROJECTOBJQUOTA) {
		propname = "objquota";
		if (!nvlist_exists(props, "objused"))
			(void) nvlist_add_uint64(props, "objused", 0);
	} else {
		return (-1);
	}
	sizeidx = us_field_index(propname);
	if (sizeidx >= 0 && sizelen > cb->cb_width[sizeidx])
		cb->cb_width[sizeidx] = sizelen;

	if (nvlist_add_uint64(props, propname, space) != 0)
		nomem();

	return (0);
}

static void
print_us_node(boolean_t scripted, boolean_t parsable, int *fields, int types,
    size_t *width, us_node_t *node)
{
	nvlist_t *nvl = node->usn_nvl;
	char valstr[MAXNAMELEN];
	boolean_t first = B_TRUE;
	int cfield = 0;
	int field;
	uint32_t ustype;

	/* Check type */
	(void) nvlist_lookup_uint32(nvl, "type", &ustype);
	if (!(ustype & types))
		return;

	while ((field = fields[cfield]) != USFIELD_LAST) {
		nvpair_t *nvp = NULL;
		data_type_t type;
		uint32_t val32;
		uint64_t val64;
		char *strval = "-";

		while ((nvp = nvlist_next_nvpair(nvl, nvp)) != NULL) {
			if (strcmp(nvpair_name(nvp),
			    us_field_names[field]) == 0)
				break;
		}

		type = nvp == NULL ? DATA_TYPE_UNKNOWN : nvpair_type(nvp);
		switch (type) {
		case DATA_TYPE_UINT32:
			(void) nvpair_value_uint32(nvp, &val32);
			break;
		case DATA_TYPE_UINT64:
			(void) nvpair_value_uint64(nvp, &val64);
			break;
		case DATA_TYPE_STRING:
			(void) nvpair_value_string(nvp, &strval);
			break;
		case DATA_TYPE_UNKNOWN:
			break;
		default:
			(void) fprintf(stderr, "invalid data type\n");
		}

		switch (field) {
		case USFIELD_TYPE:
			if (type == DATA_TYPE_UINT32)
				strval = (char *)us_type2str(val32);
			break;
		case USFIELD_NAME:
			if (type == DATA_TYPE_UINT64) {
				(void) sprintf(valstr, "%llu",
				    (u_longlong_t)val64);
				strval = valstr;
			}
			break;
		case USFIELD_USED:
		case USFIELD_QUOTA:
			if (type == DATA_TYPE_UINT64) {
				if (parsable) {
					(void) sprintf(valstr, "%llu",
					    (u_longlong_t)val64);
					strval = valstr;
				} else if (field == USFIELD_QUOTA &&
				    val64 == 0) {
					strval = "none";
				} else {
					zfs_nicebytes(val64, valstr,
					    sizeof (valstr));
					strval = valstr;
				}
			}
			break;
		case USFIELD_OBJUSED:
		case USFIELD_OBJQUOTA:
			if (type == DATA_TYPE_UINT64) {
				if (parsable) {
					(void) sprintf(valstr, "%llu",
					    (u_longlong_t)val64);
					strval = valstr;
				} else if (field == USFIELD_OBJQUOTA &&
				    val64 == 0) {
					strval = "none";
				} else {
					zfs_nicenum(val64, valstr,
					    sizeof (valstr));
					strval = valstr;
				}
			}
			break;
		}

		if (!first) {
			if (scripted)
				(void) printf("\t");
			else
				(void) printf("  ");
		}
		if (scripted)
			(void) printf("%s", strval);
		else if (field == USFIELD_TYPE || field == USFIELD_NAME)
			(void) printf("%-*s", (int)width[field], strval);
		else
			(void) printf("%*s", (int)width[field], strval);

		first = B_FALSE;
		cfield++;
	}

	(void) printf("\n");
}

static void
print_us(boolean_t scripted, boolean_t parsable, int *fields, int types,
    size_t *width, boolean_t rmnode, uu_avl_t *avl)
{
	us_node_t *node;
	const char *col;
	int cfield = 0;
	int field;

	if (!scripted) {
		boolean_t first = B_TRUE;

		while ((field = fields[cfield]) != USFIELD_LAST) {
			col = gettext(us_field_hdr[field]);
			if (field == USFIELD_TYPE || field == USFIELD_NAME) {
				(void) printf(first ? "%-*s" : "  %-*s",
				    (int)width[field], col);
			} else {
				(void) printf(first ? "%*s" : "  %*s",
				    (int)width[field], col);
			}
			first = B_FALSE;
			cfield++;
		}
		(void) printf("\n");
	}

	for (node = uu_avl_first(avl); node; node = uu_avl_next(avl, node)) {
		print_us_node(scripted, parsable, fields, types, width, node);
		if (rmnode)
			nvlist_free(node->usn_nvl);
	}
}

static int
zfs_do_userspace(int argc, char **argv)
{
	zfs_handle_t *zhp;
	zfs_userquota_prop_t p;
	uu_avl_pool_t *avl_pool;
	uu_avl_t *avl_tree;
	uu_avl_walk_t *walk;
	char *delim;
	char deffields[] = "type,name,used,quota,objused,objquota";
	char *ofield = NULL;
	char *tfield = NULL;
	int cfield = 0;
	int fields[256];
	int i;
	boolean_t scripted = B_FALSE;
	boolean_t prtnum = B_FALSE;
	boolean_t parsable = B_FALSE;
	boolean_t sid2posix = B_FALSE;
	int ret = 0;
	int c;
	zfs_sort_column_t *sortcol = NULL;
	int types = USTYPE_PSX_USR | USTYPE_SMB_USR;
	us_cbdata_t cb;
	us_node_t *node;
	us_node_t *rmnode;
	uu_list_pool_t *listpool;
	uu_list_t *list;
	uu_avl_index_t idx = 0;
	uu_list_index_t idx2 = 0;

	if (argc < 2)
		usage(B_FALSE);

	if (strcmp(argv[0], "groupspace") == 0) {
		/* Toggle default group types */
		types = USTYPE_PSX_GRP | USTYPE_SMB_GRP;
	} else if (strcmp(argv[0], "projectspace") == 0) {
		types = USTYPE_PROJ;
		prtnum = B_TRUE;
	}

	while ((c = getopt(argc, argv, "nHpo:s:S:t:i")) != -1) {
		switch (c) {
		case 'n':
			if (types == USTYPE_PROJ) {
				(void) fprintf(stderr,
				    gettext("invalid option 'n'\n"));
				usage(B_FALSE);
			}
			prtnum = B_TRUE;
			break;
		case 'H':
			scripted = B_TRUE;
			break;
		case 'p':
			parsable = B_TRUE;
			break;
		case 'o':
			ofield = optarg;
			break;
		case 's':
		case 'S':
			if (zfs_add_sort_column(&sortcol, optarg,
			    c == 's' ? B_FALSE : B_TRUE) != 0) {
				(void) fprintf(stderr,
				    gettext("invalid field '%s'\n"), optarg);
				usage(B_FALSE);
			}
			break;
		case 't':
			if (types == USTYPE_PROJ) {
				(void) fprintf(stderr,
				    gettext("invalid option 't'\n"));
				usage(B_FALSE);
			}
			tfield = optarg;
			break;
		case 'i':
			if (types == USTYPE_PROJ) {
				(void) fprintf(stderr,
				    gettext("invalid option 'i'\n"));
				usage(B_FALSE);
			}
			sid2posix = B_TRUE;
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

	if (argc < 1) {
		(void) fprintf(stderr, gettext("missing dataset name\n"));
		usage(B_FALSE);
	}
	if (argc > 1) {
		(void) fprintf(stderr, gettext("too many arguments\n"));
		usage(B_FALSE);
	}

	/* Use default output fields if not specified using -o */
	if (ofield == NULL)
		ofield = deffields;
	do {
		if ((delim = strchr(ofield, ',')) != NULL)
			*delim = '\0';
		if ((fields[cfield++] = us_field_index(ofield)) == -1) {
			(void) fprintf(stderr, gettext("invalid type '%s' "
			    "for -o option\n"), ofield);
			return (-1);
		}
		if (delim != NULL)
			ofield = delim + 1;
	} while (delim != NULL);
	fields[cfield] = USFIELD_LAST;

	/* Override output types (-t option) */
	if (tfield != NULL) {
		types = 0;

		do {
			boolean_t found = B_FALSE;

			if ((delim = strchr(tfield, ',')) != NULL)
				*delim = '\0';
			for (i = 0; i < sizeof (us_type_bits) / sizeof (int);
			    i++) {
				if (strcmp(tfield, us_type_names[i]) == 0) {
					found = B_TRUE;
					types |= us_type_bits[i];
					break;
				}
			}
			if (!found) {
				(void) fprintf(stderr, gettext("invalid type "
				    "'%s' for -t option\n"), tfield);
				return (-1);
			}
			if (delim != NULL)
				tfield = delim + 1;
		} while (delim != NULL);
	}

	if ((zhp = zfs_path_to_zhandle(g_zfs, argv[0], ZFS_TYPE_FILESYSTEM |
	    ZFS_TYPE_SNAPSHOT)) == NULL)
		return (1);
	if (zhp->zfs_head_type != ZFS_TYPE_FILESYSTEM) {
		(void) fprintf(stderr, gettext("operation is only applicable "
		    "to filesystems and their snapshots\n"));
		zfs_close(zhp);
		return (1);
	}

	if ((avl_pool = uu_avl_pool_create("us_avl_pool", sizeof (us_node_t),
	    offsetof(us_node_t, usn_avlnode), us_compare, UU_DEFAULT)) == NULL)
		nomem();
	if ((avl_tree = uu_avl_create(avl_pool, NULL, UU_DEFAULT)) == NULL)
		nomem();

	/* Always add default sorting columns */
	(void) zfs_add_sort_column(&sortcol, "type", B_FALSE);
	(void) zfs_add_sort_column(&sortcol, "name", B_FALSE);

	cb.cb_sortcol = sortcol;
	cb.cb_numname = prtnum;
	cb.cb_nicenum = !parsable;
	cb.cb_avl_pool = avl_pool;
	cb.cb_avl = avl_tree;
	cb.cb_sid2posix = sid2posix;

	for (i = 0; i < USFIELD_LAST; i++)
		cb.cb_width[i] = strlen(gettext(us_field_hdr[i]));

	for (p = 0; p < ZFS_NUM_USERQUOTA_PROPS; p++) {
		if ((zfs_prop_is_user(p) &&
		    !(types & (USTYPE_PSX_USR | USTYPE_SMB_USR))) ||
		    (zfs_prop_is_group(p) &&
		    !(types & (USTYPE_PSX_GRP | USTYPE_SMB_GRP))) ||
		    (zfs_prop_is_project(p) && types != USTYPE_PROJ))
			continue;

		cb.cb_prop = p;
		if ((ret = zfs_userspace(zhp, p, userspace_cb, &cb)) != 0) {
			zfs_close(zhp);
			return (ret);
		}
	}
	zfs_close(zhp);

	/* Sort the list */
	if ((node = uu_avl_first(avl_tree)) == NULL)
		return (0);

	us_populated = B_TRUE;

	listpool = uu_list_pool_create("tmplist", sizeof (us_node_t),
	    offsetof(us_node_t, usn_listnode), NULL, UU_DEFAULT);
	list = uu_list_create(listpool, NULL, UU_DEFAULT);
	uu_list_node_init(node, &node->usn_listnode, listpool);

	while (node != NULL) {
		rmnode = node;
		node = uu_avl_next(avl_tree, node);
		uu_avl_remove(avl_tree, rmnode);
		if (uu_list_find(list, rmnode, NULL, &idx2) == NULL)
			uu_list_insert(list, rmnode, idx2);
	}

	for (node = uu_list_first(list); node != NULL;
	    node = uu_list_next(list, node)) {
		us_sort_info_t sortinfo = { sortcol, cb.cb_numname };

		if (uu_avl_find(avl_tree, node, &sortinfo, &idx) == NULL)
			uu_avl_insert(avl_tree, node, idx);
	}

	uu_list_destroy(list);
	uu_list_pool_destroy(listpool);

	/* Print and free node nvlist memory */
	print_us(scripted, parsable, fields, types, cb.cb_width, B_TRUE,
	    cb.cb_avl);

	zfs_free_sort_columns(sortcol);

	/* Clean up the AVL tree */
	if ((walk = uu_avl_walk_start(cb.cb_avl, UU_WALK_ROBUST)) == NULL)
		nomem();

	while ((node = uu_avl_walk_next(walk)) != NULL) {
		uu_avl_remove(cb.cb_avl, node);
		free(node);
	}

	uu_avl_walk_end(walk);
	uu_avl_destroy(avl_tree);
	uu_avl_pool_destroy(avl_pool);

	return (ret);
}

/*
 * list [-Hp][-r|-d max] [-o property[,...]] [-s property] ... [-S property]
 *      [-t type[,...]] [filesystem|volume|snapshot] ...
 *
 *	-H	Scripted mode; elide headers and separate columns by tabs
 *	-p	Display values in parsable (literal) format.
 *	-r	Recurse over all children
 *	-d	Limit recursion by depth.
 *	-o	Control which fields to display.
 *	-s	Specify sort columns, descending order.
 *	-S	Specify sort columns, ascending order.
 *	-t	Control which object types to display.
 *
 * When given no arguments, list all filesystems in the system.
 * Otherwise, list the specified datasets, optionally recursing down them if
 * '-r' is specified.
 */
typedef struct list_cbdata {
	boolean_t	cb_first;
	boolean_t	cb_literal;
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
	char headerbuf[ZFS_MAXPROPLEN];
	const char *header;
	int i;
	boolean_t first = B_TRUE;
	boolean_t right_justify;

	for (; pl != NULL; pl = pl->pl_next) {
		if (!first) {
			(void) printf("  ");
		} else {
			first = B_FALSE;
		}

		right_justify = B_FALSE;
		if (pl->pl_prop != ZPROP_INVAL) {
			header = zfs_prop_column_name(pl->pl_prop);
			right_justify = zfs_prop_align_right(pl->pl_prop);
		} else {
			for (i = 0; pl->pl_user_prop[i] != '\0'; i++)
				headerbuf[i] = toupper(pl->pl_user_prop[i]);
			headerbuf[i] = '\0';
			header = headerbuf;
		}

		if (pl->pl_next == NULL && !right_justify)
			(void) printf("%s", header);
		else if (right_justify)
			(void) printf("%*s", (int)pl->pl_width, header);
		else
			(void) printf("%-*s", (int)pl->pl_width, header);
	}

	(void) printf("\n");
}

/*
 * Given a dataset and a list of fields, print out all the properties according
 * to the described layout.
 */
static void
print_dataset(zfs_handle_t *zhp, list_cbdata_t *cb)
{
	zprop_list_t *pl = cb->cb_proplist;
	boolean_t first = B_TRUE;
	char property[ZFS_MAXPROPLEN];
	nvlist_t *userprops = zfs_get_user_props(zhp);
	nvlist_t *propval;
	char *propstr;
	boolean_t right_justify;

	for (; pl != NULL; pl = pl->pl_next) {
		if (!first) {
			if (cb->cb_scripted)
				(void) printf("\t");
			else
				(void) printf("  ");
		} else {
			first = B_FALSE;
		}

		if (pl->pl_prop == ZFS_PROP_NAME) {
			(void) strlcpy(property, zfs_get_name(zhp),
			    sizeof (property));
			propstr = property;
			right_justify = zfs_prop_align_right(pl->pl_prop);
		} else if (pl->pl_prop != ZPROP_INVAL) {
			if (zfs_prop_get(zhp, pl->pl_prop, property,
			    sizeof (property), NULL, NULL, 0,
			    cb->cb_literal) != 0)
				propstr = "-";
			else
				propstr = property;
			right_justify = zfs_prop_align_right(pl->pl_prop);
		} else if (zfs_prop_userquota(pl->pl_user_prop)) {
			if (zfs_prop_get_userquota(zhp, pl->pl_user_prop,
			    property, sizeof (property), cb->cb_literal) != 0)
				propstr = "-";
			else
				propstr = property;
			right_justify = B_TRUE;
		} else if (zfs_prop_written(pl->pl_user_prop)) {
			if (zfs_prop_get_written(zhp, pl->pl_user_prop,
			    property, sizeof (property), cb->cb_literal) != 0)
				propstr = "-";
			else
				propstr = property;
			right_justify = B_TRUE;
		} else {
			if (nvlist_lookup_nvlist(userprops,
			    pl->pl_user_prop, &propval) != 0)
				propstr = "-";
			else
				verify(nvlist_lookup_string(propval,
				    ZPROP_VALUE, &propstr) == 0);
			right_justify = B_FALSE;
		}

		/*
		 * If this is being called in scripted mode, or if this is the
		 * last column and it is left-justified, don't include a width
		 * format specifier.
		 */
		if (cb->cb_scripted || (pl->pl_next == NULL && !right_justify))
			(void) printf("%s", propstr);
		else if (right_justify)
			(void) printf("%*s", (int)pl->pl_width, propstr);
		else
			(void) printf("%-*s", (int)pl->pl_width, propstr);
	}

	(void) printf("\n");
}

/*
 * Generic callback function to list a dataset or snapshot.
 */
static int
list_callback(zfs_handle_t *zhp, void *data)
{
	list_cbdata_t *cbp = data;

	if (cbp->cb_first) {
		if (!cbp->cb_scripted)
			print_header(cbp);
		cbp->cb_first = B_FALSE;
	}

	print_dataset(zhp, cbp);

	return (0);
}

static int
zfs_do_list(int argc, char **argv)
{
	int c;
	static char default_fields[] =
	    "name,used,available,referenced,mountpoint";
	int types = ZFS_TYPE_DATASET;
	boolean_t types_specified = B_FALSE;
	char *fields = NULL;
	list_cbdata_t cb = { 0 };
	char *value;
	int limit = 0;
	int ret = 0;
	zfs_sort_column_t *sortcol = NULL;
	int flags = ZFS_ITER_PROP_LISTSNAPS | ZFS_ITER_ARGS_CAN_BE_PATHS;

	/* check options */
	while ((c = getopt(argc, argv, "HS:d:o:prs:t:")) != -1) {
		switch (c) {
		case 'o':
			fields = optarg;
			break;
		case 'p':
			cb.cb_literal = B_TRUE;
			flags |= ZFS_ITER_LITERAL_PROPS;
			break;
		case 'd':
			limit = parse_depth(optarg, &flags);
			break;
		case 'r':
			flags |= ZFS_ITER_RECURSE;
			break;
		case 'H':
			cb.cb_scripted = B_TRUE;
			break;
		case 's':
			if (zfs_add_sort_column(&sortcol, optarg,
			    B_FALSE) != 0) {
				(void) fprintf(stderr,
				    gettext("invalid property '%s'\n"), optarg);
				usage(B_FALSE);
			}
			break;
		case 'S':
			if (zfs_add_sort_column(&sortcol, optarg,
			    B_TRUE) != 0) {
				(void) fprintf(stderr,
				    gettext("invalid property '%s'\n"), optarg);
				usage(B_FALSE);
			}
			break;
		case 't':
			types = 0;
			types_specified = B_TRUE;
			flags &= ~ZFS_ITER_PROP_LISTSNAPS;
			while (*optarg != '\0') {
				static char *type_subopts[] = { "filesystem",
				    "volume", "snapshot", "snap", "bookmark",
				    "all", NULL };

				switch (getsubopt(&optarg, type_subopts,
				    &value)) {
				case 0:
					types |= ZFS_TYPE_FILESYSTEM;
					break;
				case 1:
					types |= ZFS_TYPE_VOLUME;
					break;
				case 2:
				case 3:
					types |= ZFS_TYPE_SNAPSHOT;
					break;
				case 4:
					types |= ZFS_TYPE_BOOKMARK;
					break;
				case 5:
					types = ZFS_TYPE_DATASET |
					    ZFS_TYPE_BOOKMARK;
					break;
				default:
					(void) fprintf(stderr,
					    gettext("invalid type '%s'\n"),
					    value);
					usage(B_FALSE);
				}
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

	argc -= optind;
	argv += optind;

	if (fields == NULL)
		fields = default_fields;

	/*
	 * If we are only going to list snapshot names and sort by name,
	 * then we can use faster version.
	 */
	if (strcmp(fields, "name") == 0 && zfs_sort_only_by_name(sortcol))
		flags |= ZFS_ITER_SIMPLE;

	/*
	 * If "-o space" and no types were specified, don't display snapshots.
	 */
	if (strcmp(fields, "space") == 0 && types_specified == B_FALSE)
		types &= ~ZFS_TYPE_SNAPSHOT;

	/*
	 * Handle users who want to list all snapshots or bookmarks
	 * of the current dataset (ex. 'zfs list -t snapshot <dataset>').
	 */
	if ((types == ZFS_TYPE_SNAPSHOT || types == ZFS_TYPE_BOOKMARK) &&
	    argc > 0 && (flags & ZFS_ITER_RECURSE) == 0 && limit == 0) {
		flags |= (ZFS_ITER_DEPTH_LIMIT | ZFS_ITER_RECURSE);
		limit = 1;
	}

	/*
	 * If the user specifies '-o all', the zprop_get_list() doesn't
	 * normally include the name of the dataset.  For 'zfs list', we always
	 * want this property to be first.
	 */
	if (zprop_get_list(g_zfs, fields, &cb.cb_proplist, ZFS_TYPE_DATASET)
	    != 0)
		usage(B_FALSE);

	cb.cb_first = B_TRUE;

	ret = zfs_for_each(argc, argv, flags, types, sortcol, &cb.cb_proplist,
	    limit, list_callback, &cb);

	zprop_free_list(cb.cb_proplist);
	zfs_free_sort_columns(sortcol);

	if (ret == 0 && cb.cb_first && !cb.cb_scripted)
		(void) fprintf(stderr, gettext("no datasets available\n"));

	return (ret);
}

/*
 * zfs rename [-fu] <fs | snap | vol> <fs | snap | vol>
 * zfs rename [-f] -p <fs | vol> <fs | vol>
 * zfs rename [-u] -r <snap> <snap>
 *
 * Renames the given dataset to another of the same type.
 *
 * The '-p' flag creates all the non-existing ancestors of the target first.
 * The '-u' flag prevents file systems from being remounted during rename.
 */
/* ARGSUSED */
static int
zfs_do_rename(int argc, char **argv)
{
	zfs_handle_t *zhp;
	renameflags_t flags = { 0 };
	int c;
	int ret = 0;
	int types;
	boolean_t parents = B_FALSE;

	/* check options */
	while ((c = getopt(argc, argv, "pruf")) != -1) {
		switch (c) {
		case 'p':
			parents = B_TRUE;
			break;
		case 'r':
			flags.recursive = B_TRUE;
			break;
		case 'u':
			flags.nounmount = B_TRUE;
			break;
		case 'f':
			flags.forceunmount = B_TRUE;
			break;
		case '?':
		default:
			(void) fprintf(stderr, gettext("invalid option '%c'\n"),
			    optopt);
			usage(B_FALSE);
		}
	}

	argc -= optind;
	argv += optind;

	/* check number of arguments */
	if (argc < 1) {
		(void) fprintf(stderr, gettext("missing source dataset "
		    "argument\n"));
		usage(B_FALSE);
	}
	if (argc < 2) {
		(void) fprintf(stderr, gettext("missing target dataset "
		    "argument\n"));
		usage(B_FALSE);
	}
	if (argc > 2) {
		(void) fprintf(stderr, gettext("too many arguments\n"));
		usage(B_FALSE);
	}

	if (flags.recursive && parents) {
		(void) fprintf(stderr, gettext("-p and -r options are mutually "
		    "exclusive\n"));
		usage(B_FALSE);
	}

	if (flags.nounmount && parents) {
		(void) fprintf(stderr, gettext("-u and -p options are mutually "
		    "exclusive\n"));
		usage(B_FALSE);
	}

	if (flags.recursive && strchr(argv[0], '@') == 0) {
		(void) fprintf(stderr, gettext("source dataset for recursive "
		    "rename must be a snapshot\n"));
		usage(B_FALSE);
	}

	if (flags.nounmount)
		types = ZFS_TYPE_FILESYSTEM;
	else if (parents)
		types = ZFS_TYPE_FILESYSTEM | ZFS_TYPE_VOLUME;
	else
		types = ZFS_TYPE_DATASET;

	if ((zhp = zfs_open(g_zfs, argv[0], types)) == NULL)
		return (1);

	/* If we were asked and the name looks good, try to create ancestors. */
	if (parents && zfs_name_valid(argv[1], zfs_get_type(zhp)) &&
	    zfs_create_ancestors(g_zfs, argv[1]) != 0) {
		zfs_close(zhp);
		return (1);
	}

	ret = (zfs_rename(zhp, argv[1], flags) != 0);

	zfs_close(zhp);
	return (ret);
}

/*
 * zfs promote <fs>
 *
 * Promotes the given clone fs to be the parent
 */
/* ARGSUSED */
static int
zfs_do_promote(int argc, char **argv)
{
	zfs_handle_t *zhp;
	int ret = 0;

	/* check options */
	if (argc > 1 && argv[1][0] == '-') {
		(void) fprintf(stderr, gettext("invalid option '%c'\n"),
		    argv[1][1]);
		usage(B_FALSE);
	}

	/* check number of arguments */
	if (argc < 2) {
		(void) fprintf(stderr, gettext("missing clone filesystem"
		    " argument\n"));
		usage(B_FALSE);
	}
	if (argc > 2) {
		(void) fprintf(stderr, gettext("too many arguments\n"));
		usage(B_FALSE);
	}

	zhp = zfs_open(g_zfs, argv[1], ZFS_TYPE_FILESYSTEM | ZFS_TYPE_VOLUME);
	if (zhp == NULL)
		return (1);

	ret = (zfs_promote(zhp) != 0);


	zfs_close(zhp);
	return (ret);
}

static int
zfs_do_redact(int argc, char **argv)
{
	char *snap = NULL;
	char *bookname = NULL;
	char **rsnaps = NULL;
	int numrsnaps = 0;
	argv++;
	argc--;
	if (argc < 3) {
		(void) fprintf(stderr, gettext("too few arguments\n"));
		usage(B_FALSE);
	}

	snap = argv[0];
	bookname = argv[1];
	rsnaps = argv + 2;
	numrsnaps = argc - 2;

	nvlist_t *rsnapnv = fnvlist_alloc();

	for (int i = 0; i < numrsnaps; i++) {
		fnvlist_add_boolean(rsnapnv, rsnaps[i]);
	}

	int err = lzc_redact(snap, bookname, rsnapnv);
	fnvlist_free(rsnapnv);

	switch (err) {
	case 0:
		break;
	case ENOENT:
		(void) fprintf(stderr,
		    gettext("provided snapshot %s does not exist\n"), snap);
		break;
	case EEXIST:
		(void) fprintf(stderr, gettext("specified redaction bookmark "
		    "(%s) provided already exists\n"), bookname);
		break;
	case ENAMETOOLONG:
		(void) fprintf(stderr, gettext("provided bookmark name cannot "
		    "be used, final name would be too long\n"));
		break;
	case E2BIG:
		(void) fprintf(stderr, gettext("too many redaction snapshots "
		    "specified\n"));
		break;
	case EINVAL:
		if (strchr(bookname, '#') != NULL)
			(void) fprintf(stderr, gettext(
			    "redaction bookmark name must not contain '#'\n"));
		else
			(void) fprintf(stderr, gettext(
			    "redaction snapshot must be descendent of "
			    "snapshot being redacted\n"));
		break;
	case EALREADY:
		(void) fprintf(stderr, gettext("attempted to redact redacted "
		    "dataset or with respect to redacted dataset\n"));
		break;
	case ENOTSUP:
		(void) fprintf(stderr, gettext("redaction bookmarks feature "
		    "not enabled\n"));
		break;
	case EXDEV:
		(void) fprintf(stderr, gettext("potentially invalid redaction "
		    "snapshot; full dataset names required\n"));
		break;
	default:
		(void) fprintf(stderr, gettext("internal error: %s\n"),
		    strerror(errno));
	}

	return (err);
}

/*
 * zfs rollback [-rRf] <snapshot>
 *
 *	-r	Delete any intervening snapshots before doing rollback
 *	-R	Delete any snapshots and their clones
 *	-f	ignored for backwards compatibility
 *
 * Given a filesystem, rollback to a specific snapshot, discarding any changes
 * since then and making it the active dataset.  If more recent snapshots exist,
 * the command will complain unless the '-r' flag is given.
 */
typedef struct rollback_cbdata {
	uint64_t	cb_create;
	uint8_t		cb_younger_ds_printed;
	boolean_t	cb_first;
	int		cb_doclones;
	char		*cb_target;
	int		cb_error;
	boolean_t	cb_recurse;
} rollback_cbdata_t;

static int
rollback_check_dependent(zfs_handle_t *zhp, void *data)
{
	rollback_cbdata_t *cbp = data;

	if (cbp->cb_first && cbp->cb_recurse) {
		(void) fprintf(stderr, gettext("cannot rollback to "
		    "'%s': clones of previous snapshots exist\n"),
		    cbp->cb_target);
		(void) fprintf(stderr, gettext("use '-R' to "
		    "force deletion of the following clones and "
		    "dependents:\n"));
		cbp->cb_first = 0;
		cbp->cb_error = 1;
	}

	(void) fprintf(stderr, "%s\n", zfs_get_name(zhp));

	zfs_close(zhp);
	return (0);
}


/*
 * Report some snapshots/bookmarks more recent than the one specified.
 * Used when '-r' is not specified. We reuse this same callback for the
 * snapshot dependents - if 'cb_dependent' is set, then this is a
 * dependent and we should report it without checking the transaction group.
 */
static int
rollback_check(zfs_handle_t *zhp, void *data)
{
	rollback_cbdata_t *cbp = data;
	/*
	 * Max number of younger snapshots and/or bookmarks to display before
	 * we stop the iteration.
	 */
	const uint8_t max_younger = 32;

	if (cbp->cb_doclones) {
		zfs_close(zhp);
		return (0);
	}

	if (zfs_prop_get_int(zhp, ZFS_PROP_CREATETXG) > cbp->cb_create) {
		if (cbp->cb_first && !cbp->cb_recurse) {
			(void) fprintf(stderr, gettext("cannot "
			    "rollback to '%s': more recent snapshots "
			    "or bookmarks exist\n"),
			    cbp->cb_target);
			(void) fprintf(stderr, gettext("use '-r' to "
			    "force deletion of the following "
			    "snapshots and bookmarks:\n"));
			cbp->cb_first = 0;
			cbp->cb_error = 1;
		}

		if (cbp->cb_recurse) {
			if (zfs_iter_dependents(zhp, B_TRUE,
			    rollback_check_dependent, cbp) != 0) {
				zfs_close(zhp);
				return (-1);
			}
		} else {
			(void) fprintf(stderr, "%s\n",
			    zfs_get_name(zhp));
			cbp->cb_younger_ds_printed++;
		}
	}
	zfs_close(zhp);

	if (cbp->cb_younger_ds_printed == max_younger) {
		/*
		 * This non-recursive rollback is going to fail due to the
		 * presence of snapshots and/or bookmarks that are younger than
		 * the rollback target.
		 * We printed some of the offending objects, now we stop
		 * zfs_iter_snapshot/bookmark iteration so we can fail fast and
		 * avoid iterating over the rest of the younger objects
		 */
		(void) fprintf(stderr, gettext("Output limited to %d "
		    "snapshots/bookmarks\n"), max_younger);
		return (-1);
	}
	return (0);
}

static int
zfs_do_rollback(int argc, char **argv)
{
	int ret = 0;
	int c;
	boolean_t force = B_FALSE;
	rollback_cbdata_t cb = { 0 };
	zfs_handle_t *zhp, *snap;
	char parentname[ZFS_MAX_DATASET_NAME_LEN];
	char *delim;
	uint64_t min_txg = 0;

	/* check options */
	while ((c = getopt(argc, argv, "rRf")) != -1) {
		switch (c) {
		case 'r':
			cb.cb_recurse = 1;
			break;
		case 'R':
			cb.cb_recurse = 1;
			cb.cb_doclones = 1;
			break;
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

	/* check number of arguments */
	if (argc < 1) {
		(void) fprintf(stderr, gettext("missing dataset argument\n"));
		usage(B_FALSE);
	}
	if (argc > 1) {
		(void) fprintf(stderr, gettext("too many arguments\n"));
		usage(B_FALSE);
	}

	/* open the snapshot */
	if ((snap = zfs_open(g_zfs, argv[0], ZFS_TYPE_SNAPSHOT)) == NULL)
		return (1);

	/* open the parent dataset */
	(void) strlcpy(parentname, argv[0], sizeof (parentname));
	verify((delim = strrchr(parentname, '@')) != NULL);
	*delim = '\0';
	if ((zhp = zfs_open(g_zfs, parentname, ZFS_TYPE_DATASET)) == NULL) {
		zfs_close(snap);
		return (1);
	}

	/*
	 * Check for more recent snapshots and/or clones based on the presence
	 * of '-r' and '-R'.
	 */
	cb.cb_target = argv[0];
	cb.cb_create = zfs_prop_get_int(snap, ZFS_PROP_CREATETXG);
	cb.cb_first = B_TRUE;
	cb.cb_error = 0;

	if (cb.cb_create > 0)
		min_txg = cb.cb_create;

	if ((ret = zfs_iter_snapshots(zhp, B_FALSE, rollback_check, &cb,
	    min_txg, 0)) != 0)
		goto out;
	if ((ret = zfs_iter_bookmarks(zhp, rollback_check, &cb)) != 0)
		goto out;

	if ((ret = cb.cb_error) != 0)
		goto out;

	/*
	 * Rollback parent to the given snapshot.
	 */
	ret = zfs_rollback(zhp, snap, force);

out:
	zfs_close(snap);
	zfs_close(zhp);

	if (ret == 0)
		return (0);
	else
		return (1);
}

/*
 * zfs set property=value ... { fs | snap | vol } ...
 *
 * Sets the given properties for all datasets specified on the command line.
 */

static int
set_callback(zfs_handle_t *zhp, void *data)
{
	nvlist_t *props = data;

	if (zfs_prop_set_list(zhp, props) != 0) {
		switch (libzfs_errno(g_zfs)) {
		case EZFS_MOUNTFAILED:
			(void) fprintf(stderr, gettext("property may be set "
			    "but unable to remount filesystem\n"));
			break;
		case EZFS_SHARENFSFAILED:
			(void) fprintf(stderr, gettext("property may be set "
			    "but unable to reshare filesystem\n"));
			break;
		}
		return (1);
	}
	return (0);
}

static int
zfs_do_set(int argc, char **argv)
{
	nvlist_t *props = NULL;
	int ds_start = -1; /* argv idx of first dataset arg */
	int ret = 0;
	int i;

	/* check for options */
	if (argc > 1 && argv[1][0] == '-') {
		(void) fprintf(stderr, gettext("invalid option '%c'\n"),
		    argv[1][1]);
		usage(B_FALSE);
	}

	/* check number of arguments */
	if (argc < 2) {
		(void) fprintf(stderr, gettext("missing arguments\n"));
		usage(B_FALSE);
	}
	if (argc < 3) {
		if (strchr(argv[1], '=') == NULL) {
			(void) fprintf(stderr, gettext("missing property=value "
			    "argument(s)\n"));
		} else {
			(void) fprintf(stderr, gettext("missing dataset "
			    "name(s)\n"));
		}
		usage(B_FALSE);
	}

	/* validate argument order:  prop=val args followed by dataset args */
	for (i = 1; i < argc; i++) {
		if (strchr(argv[i], '=') != NULL) {
			if (ds_start > 0) {
				/* out-of-order prop=val argument */
				(void) fprintf(stderr, gettext("invalid "
				    "argument order\n"));
				usage(B_FALSE);
			}
		} else if (ds_start < 0) {
			ds_start = i;
		}
	}
	if (ds_start < 0) {
		(void) fprintf(stderr, gettext("missing dataset name(s)\n"));
		usage(B_FALSE);
	}

	/* Populate a list of property settings */
	if (nvlist_alloc(&props, NV_UNIQUE_NAME, 0) != 0)
		nomem();
	for (i = 1; i < ds_start; i++) {
		if (!parseprop(props, argv[i])) {
			ret = -1;
			goto error;
		}
	}

	ret = zfs_for_each(argc - ds_start, argv + ds_start, 0,
	    ZFS_TYPE_DATASET, NULL, NULL, 0, set_callback, props);

error:
	nvlist_free(props);
	return (ret);
}

typedef struct snap_cbdata {
	nvlist_t *sd_nvl;
	boolean_t sd_recursive;
	const char *sd_snapname;
} snap_cbdata_t;

static int
zfs_snapshot_cb(zfs_handle_t *zhp, void *arg)
{
	snap_cbdata_t *sd = arg;
	char *name;
	int rv = 0;
	int error;

	if (sd->sd_recursive &&
	    zfs_prop_get_int(zhp, ZFS_PROP_INCONSISTENT) != 0) {
		zfs_close(zhp);
		return (0);
	}

	error = asprintf(&name, "%s@%s", zfs_get_name(zhp), sd->sd_snapname);
	if (error == -1)
		nomem();
	fnvlist_add_boolean(sd->sd_nvl, name);
	free(name);

	if (sd->sd_recursive)
		rv = zfs_iter_filesystems(zhp, zfs_snapshot_cb, sd);
	zfs_close(zhp);
	return (rv);
}

/*
 * zfs snapshot [-r] [-o prop=value] ... <fs@snap>
 *
 * Creates a snapshot with the given name.  While functionally equivalent to
 * 'zfs create', it is a separate command to differentiate intent.
 */
static int
zfs_do_snapshot(int argc, char **argv)
{
	int ret = 0;
	int c;
	nvlist_t *props;
	snap_cbdata_t sd = { 0 };
	boolean_t multiple_snaps = B_FALSE;

	if (nvlist_alloc(&props, NV_UNIQUE_NAME, 0) != 0)
		nomem();
	if (nvlist_alloc(&sd.sd_nvl, NV_UNIQUE_NAME, 0) != 0)
		nomem();

	/* check options */
	while ((c = getopt(argc, argv, "ro:")) != -1) {
		switch (c) {
		case 'o':
			if (!parseprop(props, optarg)) {
				nvlist_free(sd.sd_nvl);
				nvlist_free(props);
				return (1);
			}
			break;
		case 'r':
			sd.sd_recursive = B_TRUE;
			multiple_snaps = B_TRUE;
			break;
		case '?':
			(void) fprintf(stderr, gettext("invalid option '%c'\n"),
			    optopt);
			goto usage;
		}
	}

	argc -= optind;
	argv += optind;

	/* check number of arguments */
	if (argc < 1) {
		(void) fprintf(stderr, gettext("missing snapshot argument\n"));
		goto usage;
	}

	if (argc > 1)
		multiple_snaps = B_TRUE;
	for (; argc > 0; argc--, argv++) {
		char *atp;
		zfs_handle_t *zhp;

		atp = strchr(argv[0], '@');
		if (atp == NULL)
			goto usage;
		*atp = '\0';
		sd.sd_snapname = atp + 1;
		zhp = zfs_open(g_zfs, argv[0],
		    ZFS_TYPE_FILESYSTEM | ZFS_TYPE_VOLUME);
		if (zhp == NULL)
			goto usage;
		if (zfs_snapshot_cb(zhp, &sd) != 0)
			goto usage;
	}

	ret = zfs_snapshot_nvl(g_zfs, sd.sd_nvl, props);
	nvlist_free(sd.sd_nvl);
	nvlist_free(props);
	if (ret != 0 && multiple_snaps)
		(void) fprintf(stderr, gettext("no snapshots were created\n"));
	return (ret != 0);

usage:
	nvlist_free(sd.sd_nvl);
	nvlist_free(props);
	usage(B_FALSE);
	return (-1);
}


/*
 * Send a backup stream to stdout.
 */
static int
zfs_do_send(int argc, char **argv)
{
	char *fromname = NULL;
	char *toname = NULL;
	char *resume_token = NULL;
	char *cp;
	zfs_handle_t *zhp;
	sendflags_t flags = { 0 };
	int c, err;
	nvlist_t *dbgnv = NULL;
	char *redactbook = NULL;

	struct option long_options[] = {
		{"replicate",	no_argument,		NULL, 'R'},
		{"skip-missing",	no_argument,		NULL, 's'},
		{"redact",	required_argument,	NULL, 'd'},
		{"props",	no_argument,		NULL, 'p'},
		{"parsable",	no_argument,		NULL, 'P'},
		{"dedup",	no_argument,		NULL, 'D'},
		{"verbose",	no_argument,		NULL, 'v'},
		{"dryrun",	no_argument,		NULL, 'n'},
		{"large-block",	no_argument,		NULL, 'L'},
		{"embed",	no_argument,		NULL, 'e'},
		{"resume",	required_argument,	NULL, 't'},
		{"compressed",	no_argument,		NULL, 'c'},
		{"raw",		no_argument,		NULL, 'w'},
		{"backup",	no_argument,		NULL, 'b'},
		{"holds",	no_argument,		NULL, 'h'},
		{"saved",	no_argument,		NULL, 'S'},
		{0, 0, 0, 0}
	};

	/* check options */
	while ((c = getopt_long(argc, argv, ":i:I:RsDpvnPLeht:cwbd:S",
	    long_options, NULL)) != -1) {
		switch (c) {
		case 'i':
			if (fromname)
				usage(B_FALSE);
			fromname = optarg;
			break;
		case 'I':
			if (fromname)
				usage(B_FALSE);
			fromname = optarg;
			flags.doall = B_TRUE;
			break;
		case 'R':
			flags.replicate = B_TRUE;
			break;
		case 's':
			flags.skipmissing = B_TRUE;
			break;
		case 'd':
			redactbook = optarg;
			break;
		case 'p':
			flags.props = B_TRUE;
			break;
		case 'b':
			flags.backup = B_TRUE;
			break;
		case 'h':
			flags.holds = B_TRUE;
			break;
		case 'P':
			flags.parsable = B_TRUE;
			break;
		case 'v':
			flags.verbosity++;
			flags.progress = B_TRUE;
			break;
		case 'D':
			(void) fprintf(stderr,
			    gettext("WARNING: deduplicated send is no "
			    "longer supported.  A regular,\n"
			    "non-deduplicated stream will be generated.\n\n"));
			break;
		case 'n':
			flags.dryrun = B_TRUE;
			break;
		case 'L':
			flags.largeblock = B_TRUE;
			break;
		case 'e':
			flags.embed_data = B_TRUE;
			break;
		case 't':
			resume_token = optarg;
			break;
		case 'c':
			flags.compress = B_TRUE;
			break;
		case 'w':
			flags.raw = B_TRUE;
			flags.compress = B_TRUE;
			flags.embed_data = B_TRUE;
			flags.largeblock = B_TRUE;
			break;
		case 'S':
			flags.saved = B_TRUE;
			break;
		case ':':
			/*
			 * If a parameter was not passed, optopt contains the
			 * value that would normally lead us into the
			 * appropriate case statement.  If it's > 256, then this
			 * must be a longopt and we should look at argv to get
			 * the string.  Otherwise it's just the character, so we
			 * should use it directly.
			 */
			if (optopt <= UINT8_MAX) {
				(void) fprintf(stderr,
				    gettext("missing argument for '%c' "
				    "option\n"), optopt);
			} else {
				(void) fprintf(stderr,
				    gettext("missing argument for '%s' "
				    "option\n"), argv[optind - 1]);
			}
			usage(B_FALSE);
			break;
		case '?':
			/*FALLTHROUGH*/
		default:
			/*
			 * If an invalid flag was passed, optopt contains the
			 * character if it was a short flag, or 0 if it was a
			 * longopt.
			 */
			if (optopt != 0) {
				(void) fprintf(stderr,
				    gettext("invalid option '%c'\n"), optopt);
			} else {
				(void) fprintf(stderr,
				    gettext("invalid option '%s'\n"),
				    argv[optind - 1]);

			}
			usage(B_FALSE);
		}
	}

	if (flags.parsable && flags.verbosity == 0)
		flags.verbosity = 1;

	argc -= optind;
	argv += optind;

	if (resume_token != NULL) {
		if (fromname != NULL || flags.replicate || flags.props ||
		    flags.backup || flags.holds ||
		    flags.saved || redactbook != NULL) {
			(void) fprintf(stderr,
			    gettext("invalid flags combined with -t\n"));
			usage(B_FALSE);
		}
		if (argc > 0) {
			(void) fprintf(stderr, gettext("too many arguments\n"));
			usage(B_FALSE);
		}
	} else {
		if (argc < 1) {
			(void) fprintf(stderr,
			    gettext("missing snapshot argument\n"));
			usage(B_FALSE);
		}
		if (argc > 1) {
			(void) fprintf(stderr, gettext("too many arguments\n"));
			usage(B_FALSE);
		}
	}

	if (flags.saved) {
		if (fromname != NULL || flags.replicate || flags.props ||
		    flags.doall || flags.backup ||
		    flags.holds || flags.largeblock || flags.embed_data ||
		    flags.compress || flags.raw || redactbook != NULL) {
			(void) fprintf(stderr, gettext("incompatible flags "
			    "combined with saved send flag\n"));
			usage(B_FALSE);
		}
		if (strchr(argv[0], '@') != NULL) {
			(void) fprintf(stderr, gettext("saved send must "
			    "specify the dataset with partially-received "
			    "state\n"));
			usage(B_FALSE);
		}
	}

	if (flags.raw && redactbook != NULL) {
		(void) fprintf(stderr,
		    gettext("Error: raw sends may not be redacted.\n"));
		return (1);
	}

	if (!flags.dryrun && isatty(STDOUT_FILENO)) {
		(void) fprintf(stderr,
		    gettext("Error: Stream can not be written to a terminal.\n"
		    "You must redirect standard output.\n"));
		return (1);
	}

	if (flags.saved) {
		zhp = zfs_open(g_zfs, argv[0], ZFS_TYPE_DATASET);
		if (zhp == NULL)
			return (1);

		err = zfs_send_saved(zhp, &flags, STDOUT_FILENO,
		    resume_token);
		if (err != 0)
			note_dev_error(errno, STDOUT_FILENO);
		zfs_close(zhp);
		return (err != 0);
	} else if (resume_token != NULL) {
		err = zfs_send_resume(g_zfs, &flags, STDOUT_FILENO,
		    resume_token);
		if (err != 0)
			note_dev_error(errno, STDOUT_FILENO);
		return (err);
	}

	if (flags.skipmissing && !flags.replicate) {
		(void) fprintf(stderr,
		    gettext("skip-missing flag can only be used in "
		    "conjunction with replicate\n"));
		usage(B_FALSE);
	}

	/*
	 * For everything except -R and -I, use the new, cleaner code path.
	 */
	if (!(flags.replicate || flags.doall)) {
		char frombuf[ZFS_MAX_DATASET_NAME_LEN];

		if (fromname != NULL && (strchr(fromname, '#') == NULL &&
		    strchr(fromname, '@') == NULL)) {
			/*
			 * Neither bookmark or snapshot was specified.  Print a
			 * warning, and assume snapshot.
			 */
			(void) fprintf(stderr, "Warning: incremental source "
			    "didn't specify type, assuming snapshot. Use '@' "
			    "or '#' prefix to avoid ambiguity.\n");
			(void) snprintf(frombuf, sizeof (frombuf), "@%s",
			    fromname);
			fromname = frombuf;
		}
		if (fromname != NULL &&
		    (fromname[0] == '#' || fromname[0] == '@')) {
			/*
			 * Incremental source name begins with # or @.
			 * Default to same fs as target.
			 */
			char tmpbuf[ZFS_MAX_DATASET_NAME_LEN];
			(void) strlcpy(tmpbuf, fromname, sizeof (tmpbuf));
			(void) strlcpy(frombuf, argv[0], sizeof (frombuf));
			cp = strchr(frombuf, '@');
			if (cp != NULL)
				*cp = '\0';
			(void) strlcat(frombuf, tmpbuf, sizeof (frombuf));
			fromname = frombuf;
		}

		zhp = zfs_open(g_zfs, argv[0], ZFS_TYPE_DATASET);
		if (zhp == NULL)
			return (1);
		err = zfs_send_one(zhp, fromname, STDOUT_FILENO, &flags,
		    redactbook);
		zfs_close(zhp);
		if (err != 0)
			note_dev_error(errno, STDOUT_FILENO);
		return (err != 0);
	}

	if (fromname != NULL && strchr(fromname, '#')) {
		(void) fprintf(stderr,
		    gettext("Error: multiple snapshots cannot be "
		    "sent from a bookmark.\n"));
		return (1);
	}

	if (redactbook != NULL) {
		(void) fprintf(stderr, gettext("Error: multiple snapshots "
		    "cannot be sent redacted.\n"));
		return (1);
	}

	if ((cp = strchr(argv[0], '@')) == NULL) {
		(void) fprintf(stderr, gettext("Error: "
		    "Unsupported flag with filesystem or bookmark.\n"));
		return (1);
	}
	*cp = '\0';
	toname = cp + 1;
	zhp = zfs_open(g_zfs, argv[0], ZFS_TYPE_FILESYSTEM | ZFS_TYPE_VOLUME);
	if (zhp == NULL)
		return (1);

	/*
	 * If they specified the full path to the snapshot, chop off
	 * everything except the short name of the snapshot, but special
	 * case if they specify the origin.
	 */
	if (fromname && (cp = strchr(fromname, '@')) != NULL) {
		char origin[ZFS_MAX_DATASET_NAME_LEN];
		zprop_source_t src;

		(void) zfs_prop_get(zhp, ZFS_PROP_ORIGIN,
		    origin, sizeof (origin), &src, NULL, 0, B_FALSE);

		if (strcmp(origin, fromname) == 0) {
			fromname = NULL;
			flags.fromorigin = B_TRUE;
		} else {
			*cp = '\0';
			if (cp != fromname && strcmp(argv[0], fromname)) {
				(void) fprintf(stderr,
				    gettext("incremental source must be "
				    "in same filesystem\n"));
				usage(B_FALSE);
			}
			fromname = cp + 1;
			if (strchr(fromname, '@') || strchr(fromname, '/')) {
				(void) fprintf(stderr,
				    gettext("invalid incremental source\n"));
				usage(B_FALSE);
			}
		}
	}

	if (flags.replicate && fromname == NULL)
		flags.doall = B_TRUE;

	err = zfs_send(zhp, fromname, toname, &flags, STDOUT_FILENO, NULL, 0,
	    flags.verbosity >= 3 ? &dbgnv : NULL);

	if (flags.verbosity >= 3 && dbgnv != NULL) {
		/*
		 * dump_nvlist prints to stdout, but that's been
		 * redirected to a file.  Make it print to stderr
		 * instead.
		 */
		(void) dup2(STDERR_FILENO, STDOUT_FILENO);
		dump_nvlist(dbgnv, 0);
		nvlist_free(dbgnv);
	}
	zfs_close(zhp);
	note_dev_error(errno, STDOUT_FILENO);

	return (err != 0);
}

/*
 * Restore a backup stream from stdin.
 */
static int
zfs_do_receive(int argc, char **argv)
{
	int c, err = 0;
	recvflags_t flags = { 0 };
	boolean_t abort_resumable = B_FALSE;
	nvlist_t *props;

	if (nvlist_alloc(&props, NV_UNIQUE_NAME, 0) != 0)
		nomem();

	/* check options */
	while ((c = getopt(argc, argv, ":o:x:dehMnuvFsA")) != -1) {
		switch (c) {
		case 'o':
			if (!parseprop(props, optarg)) {
				nvlist_free(props);
				usage(B_FALSE);
			}
			break;
		case 'x':
			if (!parsepropname(props, optarg)) {
				nvlist_free(props);
				usage(B_FALSE);
			}
			break;
		case 'd':
			if (flags.istail) {
				(void) fprintf(stderr, gettext("invalid option "
				    "combination: -d and -e are mutually "
				    "exclusive\n"));
				usage(B_FALSE);
			}
			flags.isprefix = B_TRUE;
			break;
		case 'e':
			if (flags.isprefix) {
				(void) fprintf(stderr, gettext("invalid option "
				    "combination: -d and -e are mutually "
				    "exclusive\n"));
				usage(B_FALSE);
			}
			flags.istail = B_TRUE;
			break;
		case 'h':
			flags.skipholds = B_TRUE;
			break;
		case 'M':
			flags.forceunmount = B_TRUE;
			break;
		case 'n':
			flags.dryrun = B_TRUE;
			break;
		case 'u':
			flags.nomount = B_TRUE;
			break;
		case 'v':
			flags.verbose = B_TRUE;
			break;
		case 's':
			flags.resumable = B_TRUE;
			break;
		case 'F':
			flags.force = B_TRUE;
			break;
		case 'A':
			abort_resumable = B_TRUE;
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

	/* zfs recv -e (use "tail" name) implies -d (remove dataset "head") */
	if (flags.istail)
		flags.isprefix = B_TRUE;

	/* check number of arguments */
	if (argc < 1) {
		(void) fprintf(stderr, gettext("missing snapshot argument\n"));
		usage(B_FALSE);
	}
	if (argc > 1) {
		(void) fprintf(stderr, gettext("too many arguments\n"));
		usage(B_FALSE);
	}

	if (abort_resumable) {
		if (flags.isprefix || flags.istail || flags.dryrun ||
		    flags.resumable || flags.nomount) {
			(void) fprintf(stderr, gettext("invalid option\n"));
			usage(B_FALSE);
		}

		char namebuf[ZFS_MAX_DATASET_NAME_LEN];
		(void) snprintf(namebuf, sizeof (namebuf),
		    "%s/%%recv", argv[0]);

		if (zfs_dataset_exists(g_zfs, namebuf,
		    ZFS_TYPE_FILESYSTEM | ZFS_TYPE_VOLUME)) {
			zfs_handle_t *zhp = zfs_open(g_zfs,
			    namebuf, ZFS_TYPE_FILESYSTEM | ZFS_TYPE_VOLUME);
			if (zhp == NULL) {
				nvlist_free(props);
				return (1);
			}
			err = zfs_destroy(zhp, B_FALSE);
			zfs_close(zhp);
		} else {
			zfs_handle_t *zhp = zfs_open(g_zfs,
			    argv[0], ZFS_TYPE_FILESYSTEM | ZFS_TYPE_VOLUME);
			if (zhp == NULL)
				usage(B_FALSE);
			if (!zfs_prop_get_int(zhp, ZFS_PROP_INCONSISTENT) ||
			    zfs_prop_get(zhp, ZFS_PROP_RECEIVE_RESUME_TOKEN,
			    NULL, 0, NULL, NULL, 0, B_TRUE) == -1) {
				(void) fprintf(stderr,
				    gettext("'%s' does not have any "
				    "resumable receive state to abort\n"),
				    argv[0]);
				nvlist_free(props);
				zfs_close(zhp);
				return (1);
			}
			err = zfs_destroy(zhp, B_FALSE);
			zfs_close(zhp);
		}
		nvlist_free(props);
		return (err != 0);
	}

	if (isatty(STDIN_FILENO)) {
		(void) fprintf(stderr,
		    gettext("Error: Backup stream can not be read "
		    "from a terminal.\n"
		    "You must redirect standard input.\n"));
		nvlist_free(props);
		return (1);
	}
	err = zfs_receive(g_zfs, argv[0], props, &flags, STDIN_FILENO, NULL);
	nvlist_free(props);

	return (err != 0);
}

/*
 * allow/unallow stuff
 */
/* copied from zfs/sys/dsl_deleg.h */
#define	ZFS_DELEG_PERM_CREATE		"create"
#define	ZFS_DELEG_PERM_DESTROY		"destroy"
#define	ZFS_DELEG_PERM_SNAPSHOT		"snapshot"
#define	ZFS_DELEG_PERM_ROLLBACK		"rollback"
#define	ZFS_DELEG_PERM_CLONE		"clone"
#define	ZFS_DELEG_PERM_PROMOTE		"promote"
#define	ZFS_DELEG_PERM_RENAME		"rename"
#define	ZFS_DELEG_PERM_MOUNT		"mount"
#define	ZFS_DELEG_PERM_SHARE		"share"
#define	ZFS_DELEG_PERM_SEND		"send"
#define	ZFS_DELEG_PERM_RECEIVE		"receive"
#define	ZFS_DELEG_PERM_ALLOW		"allow"
#define	ZFS_DELEG_PERM_USERPROP		"userprop"
#define	ZFS_DELEG_PERM_VSCAN		"vscan" /* ??? */
#define	ZFS_DELEG_PERM_USERQUOTA	"userquota"
#define	ZFS_DELEG_PERM_GROUPQUOTA	"groupquota"
#define	ZFS_DELEG_PERM_USERUSED		"userused"
#define	ZFS_DELEG_PERM_GROUPUSED	"groupused"
#define	ZFS_DELEG_PERM_USEROBJQUOTA	"userobjquota"
#define	ZFS_DELEG_PERM_GROUPOBJQUOTA	"groupobjquota"
#define	ZFS_DELEG_PERM_USEROBJUSED	"userobjused"
#define	ZFS_DELEG_PERM_GROUPOBJUSED	"groupobjused"

#define	ZFS_DELEG_PERM_HOLD		"hold"
#define	ZFS_DELEG_PERM_RELEASE		"release"
#define	ZFS_DELEG_PERM_DIFF		"diff"
#define	ZFS_DELEG_PERM_BOOKMARK		"bookmark"
#define	ZFS_DELEG_PERM_LOAD_KEY		"load-key"
#define	ZFS_DELEG_PERM_CHANGE_KEY	"change-key"

#define	ZFS_DELEG_PERM_PROJECTUSED	"projectused"
#define	ZFS_DELEG_PERM_PROJECTQUOTA	"projectquota"
#define	ZFS_DELEG_PERM_PROJECTOBJUSED	"projectobjused"
#define	ZFS_DELEG_PERM_PROJECTOBJQUOTA	"projectobjquota"

#define	ZFS_NUM_DELEG_NOTES ZFS_DELEG_NOTE_NONE

static zfs_deleg_perm_tab_t zfs_deleg_perm_tbl[] = {
	{ ZFS_DELEG_PERM_ALLOW, ZFS_DELEG_NOTE_ALLOW },
	{ ZFS_DELEG_PERM_CLONE, ZFS_DELEG_NOTE_CLONE },
	{ ZFS_DELEG_PERM_CREATE, ZFS_DELEG_NOTE_CREATE },
	{ ZFS_DELEG_PERM_DESTROY, ZFS_DELEG_NOTE_DESTROY },
	{ ZFS_DELEG_PERM_DIFF, ZFS_DELEG_NOTE_DIFF},
	{ ZFS_DELEG_PERM_HOLD, ZFS_DELEG_NOTE_HOLD },
	{ ZFS_DELEG_PERM_MOUNT, ZFS_DELEG_NOTE_MOUNT },
	{ ZFS_DELEG_PERM_PROMOTE, ZFS_DELEG_NOTE_PROMOTE },
	{ ZFS_DELEG_PERM_RECEIVE, ZFS_DELEG_NOTE_RECEIVE },
	{ ZFS_DELEG_PERM_RELEASE, ZFS_DELEG_NOTE_RELEASE },
	{ ZFS_DELEG_PERM_RENAME, ZFS_DELEG_NOTE_RENAME },
	{ ZFS_DELEG_PERM_ROLLBACK, ZFS_DELEG_NOTE_ROLLBACK },
	{ ZFS_DELEG_PERM_SEND, ZFS_DELEG_NOTE_SEND },
	{ ZFS_DELEG_PERM_SHARE, ZFS_DELEG_NOTE_SHARE },
	{ ZFS_DELEG_PERM_SNAPSHOT, ZFS_DELEG_NOTE_SNAPSHOT },
	{ ZFS_DELEG_PERM_BOOKMARK, ZFS_DELEG_NOTE_BOOKMARK },
	{ ZFS_DELEG_PERM_LOAD_KEY, ZFS_DELEG_NOTE_LOAD_KEY },
	{ ZFS_DELEG_PERM_CHANGE_KEY, ZFS_DELEG_NOTE_CHANGE_KEY },

	{ ZFS_DELEG_PERM_GROUPQUOTA, ZFS_DELEG_NOTE_GROUPQUOTA },
	{ ZFS_DELEG_PERM_GROUPUSED, ZFS_DELEG_NOTE_GROUPUSED },
	{ ZFS_DELEG_PERM_USERPROP, ZFS_DELEG_NOTE_USERPROP },
	{ ZFS_DELEG_PERM_USERQUOTA, ZFS_DELEG_NOTE_USERQUOTA },
	{ ZFS_DELEG_PERM_USERUSED, ZFS_DELEG_NOTE_USERUSED },
	{ ZFS_DELEG_PERM_USEROBJQUOTA, ZFS_DELEG_NOTE_USEROBJQUOTA },
	{ ZFS_DELEG_PERM_USEROBJUSED, ZFS_DELEG_NOTE_USEROBJUSED },
	{ ZFS_DELEG_PERM_GROUPOBJQUOTA, ZFS_DELEG_NOTE_GROUPOBJQUOTA },
	{ ZFS_DELEG_PERM_GROUPOBJUSED, ZFS_DELEG_NOTE_GROUPOBJUSED },
	{ ZFS_DELEG_PERM_PROJECTUSED, ZFS_DELEG_NOTE_PROJECTUSED },
	{ ZFS_DELEG_PERM_PROJECTQUOTA, ZFS_DELEG_NOTE_PROJECTQUOTA },
	{ ZFS_DELEG_PERM_PROJECTOBJUSED, ZFS_DELEG_NOTE_PROJECTOBJUSED },
	{ ZFS_DELEG_PERM_PROJECTOBJQUOTA, ZFS_DELEG_NOTE_PROJECTOBJQUOTA },
	{ NULL, ZFS_DELEG_NOTE_NONE }
};

/* permission structure */
typedef struct deleg_perm {
	zfs_deleg_who_type_t	dp_who_type;
	const char		*dp_name;
	boolean_t		dp_local;
	boolean_t		dp_descend;
} deleg_perm_t;

/* */
typedef struct deleg_perm_node {
	deleg_perm_t		dpn_perm;

	uu_avl_node_t		dpn_avl_node;
} deleg_perm_node_t;

typedef struct fs_perm fs_perm_t;

/* permissions set */
typedef struct who_perm {
	zfs_deleg_who_type_t	who_type;
	const char		*who_name;		/* id */
	char			who_ug_name[256];	/* user/group name */
	fs_perm_t		*who_fsperm;		/* uplink */

	uu_avl_t		*who_deleg_perm_avl;	/* permissions */
} who_perm_t;

/* */
typedef struct who_perm_node {
	who_perm_t	who_perm;
	uu_avl_node_t	who_avl_node;
} who_perm_node_t;

typedef struct fs_perm_set fs_perm_set_t;
/* fs permissions */
struct fs_perm {
	const char		*fsp_name;

	uu_avl_t		*fsp_sc_avl;	/* sets,create */
	uu_avl_t		*fsp_uge_avl;	/* user,group,everyone */

	fs_perm_set_t		*fsp_set;	/* uplink */
};

/* */
typedef struct fs_perm_node {
	fs_perm_t	fspn_fsperm;
	uu_avl_t	*fspn_avl;

	uu_list_node_t	fspn_list_node;
} fs_perm_node_t;

/* top level structure */
struct fs_perm_set {
	uu_list_pool_t	*fsps_list_pool;
	uu_list_t	*fsps_list; /* list of fs_perms */

	uu_avl_pool_t	*fsps_named_set_avl_pool;
	uu_avl_pool_t	*fsps_who_perm_avl_pool;
	uu_avl_pool_t	*fsps_deleg_perm_avl_pool;
};

static inline const char *
deleg_perm_type(zfs_deleg_note_t note)
{
	/* subcommands */
	switch (note) {
		/* SUBCOMMANDS */
		/* OTHER */
	case ZFS_DELEG_NOTE_GROUPQUOTA:
	case ZFS_DELEG_NOTE_GROUPUSED:
	case ZFS_DELEG_NOTE_USERPROP:
	case ZFS_DELEG_NOTE_USERQUOTA:
	case ZFS_DELEG_NOTE_USERUSED:
	case ZFS_DELEG_NOTE_USEROBJQUOTA:
	case ZFS_DELEG_NOTE_USEROBJUSED:
	case ZFS_DELEG_NOTE_GROUPOBJQUOTA:
	case ZFS_DELEG_NOTE_GROUPOBJUSED:
	case ZFS_DELEG_NOTE_PROJECTUSED:
	case ZFS_DELEG_NOTE_PROJECTQUOTA:
	case ZFS_DELEG_NOTE_PROJECTOBJUSED:
	case ZFS_DELEG_NOTE_PROJECTOBJQUOTA:
		/* other */
		return (gettext("other"));
	default:
		return (gettext("subcommand"));
	}
}

static int
who_type2weight(zfs_deleg_who_type_t who_type)
{
	int res;
	switch (who_type) {
		case ZFS_DELEG_NAMED_SET_SETS:
		case ZFS_DELEG_NAMED_SET:
			res = 0;
			break;
		case ZFS_DELEG_CREATE_SETS:
		case ZFS_DELEG_CREATE:
			res = 1;
			break;
		case ZFS_DELEG_USER_SETS:
		case ZFS_DELEG_USER:
			res = 2;
			break;
		case ZFS_DELEG_GROUP_SETS:
		case ZFS_DELEG_GROUP:
			res = 3;
			break;
		case ZFS_DELEG_EVERYONE_SETS:
		case ZFS_DELEG_EVERYONE:
			res = 4;
			break;
		default:
			res = -1;
	}

	return (res);
}

/* ARGSUSED */
static int
who_perm_compare(const void *larg, const void *rarg, void *unused)
{
	const who_perm_node_t *l = larg;
	const who_perm_node_t *r = rarg;
	zfs_deleg_who_type_t ltype = l->who_perm.who_type;
	zfs_deleg_who_type_t rtype = r->who_perm.who_type;
	int lweight = who_type2weight(ltype);
	int rweight = who_type2weight(rtype);
	int res = lweight - rweight;
	if (res == 0)
		res = strncmp(l->who_perm.who_name, r->who_perm.who_name,
		    ZFS_MAX_DELEG_NAME-1);

	if (res == 0)
		return (0);
	if (res > 0)
		return (1);
	else
		return (-1);
}

/* ARGSUSED */
static int
deleg_perm_compare(const void *larg, const void *rarg, void *unused)
{
	const deleg_perm_node_t *l = larg;
	const deleg_perm_node_t *r = rarg;
	int res =  strncmp(l->dpn_perm.dp_name, r->dpn_perm.dp_name,
	    ZFS_MAX_DELEG_NAME-1);

	if (res == 0)
		return (0);

	if (res > 0)
		return (1);
	else
		return (-1);
}

static inline void
fs_perm_set_init(fs_perm_set_t *fspset)
{
	bzero(fspset, sizeof (fs_perm_set_t));

	if ((fspset->fsps_list_pool = uu_list_pool_create("fsps_list_pool",
	    sizeof (fs_perm_node_t), offsetof(fs_perm_node_t, fspn_list_node),
	    NULL, UU_DEFAULT)) == NULL)
		nomem();
	if ((fspset->fsps_list = uu_list_create(fspset->fsps_list_pool, NULL,
	    UU_DEFAULT)) == NULL)
		nomem();

	if ((fspset->fsps_named_set_avl_pool = uu_avl_pool_create(
	    "named_set_avl_pool", sizeof (who_perm_node_t), offsetof(
	    who_perm_node_t, who_avl_node), who_perm_compare,
	    UU_DEFAULT)) == NULL)
		nomem();

	if ((fspset->fsps_who_perm_avl_pool = uu_avl_pool_create(
	    "who_perm_avl_pool", sizeof (who_perm_node_t), offsetof(
	    who_perm_node_t, who_avl_node), who_perm_compare,
	    UU_DEFAULT)) == NULL)
		nomem();

	if ((fspset->fsps_deleg_perm_avl_pool = uu_avl_pool_create(
	    "deleg_perm_avl_pool", sizeof (deleg_perm_node_t), offsetof(
	    deleg_perm_node_t, dpn_avl_node), deleg_perm_compare, UU_DEFAULT))
	    == NULL)
		nomem();
}

static inline void fs_perm_fini(fs_perm_t *);
static inline void who_perm_fini(who_perm_t *);

static inline void
fs_perm_set_fini(fs_perm_set_t *fspset)
{
	fs_perm_node_t *node = uu_list_first(fspset->fsps_list);

	while (node != NULL) {
		fs_perm_node_t *next_node =
		    uu_list_next(fspset->fsps_list, node);
		fs_perm_t *fsperm = &node->fspn_fsperm;
		fs_perm_fini(fsperm);
		uu_list_remove(fspset->fsps_list, node);
		free(node);
		node = next_node;
	}

	uu_avl_pool_destroy(fspset->fsps_named_set_avl_pool);
	uu_avl_pool_destroy(fspset->fsps_who_perm_avl_pool);
	uu_avl_pool_destroy(fspset->fsps_deleg_perm_avl_pool);
}

static inline void
deleg_perm_init(deleg_perm_t *deleg_perm, zfs_deleg_who_type_t type,
    const char *name)
{
	deleg_perm->dp_who_type = type;
	deleg_perm->dp_name = name;
}

static inline void
who_perm_init(who_perm_t *who_perm, fs_perm_t *fsperm,
    zfs_deleg_who_type_t type, const char *name)
{
	uu_avl_pool_t	*pool;
	pool = fsperm->fsp_set->fsps_deleg_perm_avl_pool;

	bzero(who_perm, sizeof (who_perm_t));

	if ((who_perm->who_deleg_perm_avl = uu_avl_create(pool, NULL,
	    UU_DEFAULT)) == NULL)
		nomem();

	who_perm->who_type = type;
	who_perm->who_name = name;
	who_perm->who_fsperm = fsperm;
}

static inline void
who_perm_fini(who_perm_t *who_perm)
{
	deleg_perm_node_t *node = uu_avl_first(who_perm->who_deleg_perm_avl);

	while (node != NULL) {
		deleg_perm_node_t *next_node =
		    uu_avl_next(who_perm->who_deleg_perm_avl, node);

		uu_avl_remove(who_perm->who_deleg_perm_avl, node);
		free(node);
		node = next_node;
	}

	uu_avl_destroy(who_perm->who_deleg_perm_avl);
}

static inline void
fs_perm_init(fs_perm_t *fsperm, fs_perm_set_t *fspset, const char *fsname)
{
	uu_avl_pool_t	*nset_pool = fspset->fsps_named_set_avl_pool;
	uu_avl_pool_t	*who_pool = fspset->fsps_who_perm_avl_pool;

	bzero(fsperm, sizeof (fs_perm_t));

	if ((fsperm->fsp_sc_avl = uu_avl_create(nset_pool, NULL, UU_DEFAULT))
	    == NULL)
		nomem();

	if ((fsperm->fsp_uge_avl = uu_avl_create(who_pool, NULL, UU_DEFAULT))
	    == NULL)
		nomem();

	fsperm->fsp_set = fspset;
	fsperm->fsp_name = fsname;
}

static inline void
fs_perm_fini(fs_perm_t *fsperm)
{
	who_perm_node_t *node = uu_avl_first(fsperm->fsp_sc_avl);
	while (node != NULL) {
		who_perm_node_t *next_node = uu_avl_next(fsperm->fsp_sc_avl,
		    node);
		who_perm_t *who_perm = &node->who_perm;
		who_perm_fini(who_perm);
		uu_avl_remove(fsperm->fsp_sc_avl, node);
		free(node);
		node = next_node;
	}

	node = uu_avl_first(fsperm->fsp_uge_avl);
	while (node != NULL) {
		who_perm_node_t *next_node = uu_avl_next(fsperm->fsp_uge_avl,
		    node);
		who_perm_t *who_perm = &node->who_perm;
		who_perm_fini(who_perm);
		uu_avl_remove(fsperm->fsp_uge_avl, node);
		free(node);
		node = next_node;
	}

	uu_avl_destroy(fsperm->fsp_sc_avl);
	uu_avl_destroy(fsperm->fsp_uge_avl);
}

static void
set_deleg_perm_node(uu_avl_t *avl, deleg_perm_node_t *node,
    zfs_deleg_who_type_t who_type, const char *name, char locality)
{
	uu_avl_index_t idx = 0;

	deleg_perm_node_t *found_node = NULL;
	deleg_perm_t	*deleg_perm = &node->dpn_perm;

	deleg_perm_init(deleg_perm, who_type, name);

	if ((found_node = uu_avl_find(avl, node, NULL, &idx))
	    == NULL)
		uu_avl_insert(avl, node, idx);
	else {
		node = found_node;
		deleg_perm = &node->dpn_perm;
	}


	switch (locality) {
	case ZFS_DELEG_LOCAL:
		deleg_perm->dp_local = B_TRUE;
		break;
	case ZFS_DELEG_DESCENDENT:
		deleg_perm->dp_descend = B_TRUE;
		break;
	case ZFS_DELEG_NA:
		break;
	default:
		assert(B_FALSE); /* invalid locality */
	}
}

static inline int
parse_who_perm(who_perm_t *who_perm, nvlist_t *nvl, char locality)
{
	nvpair_t *nvp = NULL;
	fs_perm_set_t *fspset = who_perm->who_fsperm->fsp_set;
	uu_avl_t *avl = who_perm->who_deleg_perm_avl;
	zfs_deleg_who_type_t who_type = who_perm->who_type;

	while ((nvp = nvlist_next_nvpair(nvl, nvp)) != NULL) {
		const char *name = nvpair_name(nvp);
		data_type_t type = nvpair_type(nvp);
		uu_avl_pool_t *avl_pool = fspset->fsps_deleg_perm_avl_pool;
		deleg_perm_node_t *node =
		    safe_malloc(sizeof (deleg_perm_node_t));

		VERIFY(type == DATA_TYPE_BOOLEAN);

		uu_avl_node_init(node, &node->dpn_avl_node, avl_pool);
		set_deleg_perm_node(avl, node, who_type, name, locality);
	}

	return (0);
}

static inline int
parse_fs_perm(fs_perm_t *fsperm, nvlist_t *nvl)
{
	nvpair_t *nvp = NULL;
	fs_perm_set_t *fspset = fsperm->fsp_set;

	while ((nvp = nvlist_next_nvpair(nvl, nvp)) != NULL) {
		nvlist_t *nvl2 = NULL;
		const char *name = nvpair_name(nvp);
		uu_avl_t *avl = NULL;
		uu_avl_pool_t *avl_pool = NULL;
		zfs_deleg_who_type_t perm_type = name[0];
		char perm_locality = name[1];
		const char *perm_name = name + 3;
		who_perm_t *who_perm = NULL;

		assert('$' == name[2]);

		if (nvpair_value_nvlist(nvp, &nvl2) != 0)
			return (-1);

		switch (perm_type) {
		case ZFS_DELEG_CREATE:
		case ZFS_DELEG_CREATE_SETS:
		case ZFS_DELEG_NAMED_SET:
		case ZFS_DELEG_NAMED_SET_SETS:
			avl_pool = fspset->fsps_named_set_avl_pool;
			avl = fsperm->fsp_sc_avl;
			break;
		case ZFS_DELEG_USER:
		case ZFS_DELEG_USER_SETS:
		case ZFS_DELEG_GROUP:
		case ZFS_DELEG_GROUP_SETS:
		case ZFS_DELEG_EVERYONE:
		case ZFS_DELEG_EVERYONE_SETS:
			avl_pool = fspset->fsps_who_perm_avl_pool;
			avl = fsperm->fsp_uge_avl;
			break;

		default:
			assert(!"unhandled zfs_deleg_who_type_t");
		}

		who_perm_node_t *found_node = NULL;
		who_perm_node_t *node = safe_malloc(
		    sizeof (who_perm_node_t));
		who_perm = &node->who_perm;
		uu_avl_index_t idx = 0;

		uu_avl_node_init(node, &node->who_avl_node, avl_pool);
		who_perm_init(who_perm, fsperm, perm_type, perm_name);

		if ((found_node = uu_avl_find(avl, node, NULL, &idx))
		    == NULL) {
			if (avl == fsperm->fsp_uge_avl) {
				uid_t rid = 0;
				struct passwd *p = NULL;
				struct group *g = NULL;
				const char *nice_name = NULL;

				switch (perm_type) {
				case ZFS_DELEG_USER_SETS:
				case ZFS_DELEG_USER:
					rid = atoi(perm_name);
					p = getpwuid(rid);
					if (p)
						nice_name = p->pw_name;
					break;
				case ZFS_DELEG_GROUP_SETS:
				case ZFS_DELEG_GROUP:
					rid = atoi(perm_name);
					g = getgrgid(rid);
					if (g)
						nice_name = g->gr_name;
					break;

				default:
					break;
				}

				if (nice_name != NULL) {
					(void) strlcpy(
					    node->who_perm.who_ug_name,
					    nice_name, 256);
				} else {
					/* User or group unknown */
					(void) snprintf(
					    node->who_perm.who_ug_name,
					    sizeof (node->who_perm.who_ug_name),
					    "(unknown: %d)", rid);
				}
			}

			uu_avl_insert(avl, node, idx);
		} else {
			node = found_node;
			who_perm = &node->who_perm;
		}

		assert(who_perm != NULL);
		(void) parse_who_perm(who_perm, nvl2, perm_locality);
	}

	return (0);
}

static inline int
parse_fs_perm_set(fs_perm_set_t *fspset, nvlist_t *nvl)
{
	nvpair_t *nvp = NULL;
	uu_avl_index_t idx = 0;

	while ((nvp = nvlist_next_nvpair(nvl, nvp)) != NULL) {
		nvlist_t *nvl2 = NULL;
		const char *fsname = nvpair_name(nvp);
		data_type_t type = nvpair_type(nvp);
		fs_perm_t *fsperm = NULL;
		fs_perm_node_t *node = safe_malloc(sizeof (fs_perm_node_t));
		if (node == NULL)
			nomem();

		fsperm = &node->fspn_fsperm;

		VERIFY(DATA_TYPE_NVLIST == type);

		uu_list_node_init(node, &node->fspn_list_node,
		    fspset->fsps_list_pool);

		idx = uu_list_numnodes(fspset->fsps_list);
		fs_perm_init(fsperm, fspset, fsname);

		if (nvpair_value_nvlist(nvp, &nvl2) != 0)
			return (-1);

		(void) parse_fs_perm(fsperm, nvl2);

		uu_list_insert(fspset->fsps_list, node, idx);
	}

	return (0);
}

static inline const char *
deleg_perm_comment(zfs_deleg_note_t note)
{
	const char *str = "";

	/* subcommands */
	switch (note) {
		/* SUBCOMMANDS */
	case ZFS_DELEG_NOTE_ALLOW:
		str = gettext("Must also have the permission that is being"
		    "\n\t\t\t\tallowed");
		break;
	case ZFS_DELEG_NOTE_CLONE:
		str = gettext("Must also have the 'create' ability and 'mount'"
		    "\n\t\t\t\tability in the origin file system");
		break;
	case ZFS_DELEG_NOTE_CREATE:
		str = gettext("Must also have the 'mount' ability");
		break;
	case ZFS_DELEG_NOTE_DESTROY:
		str = gettext("Must also have the 'mount' ability");
		break;
	case ZFS_DELEG_NOTE_DIFF:
		str = gettext("Allows lookup of paths within a dataset;"
		    "\n\t\t\t\tgiven an object number. Ordinary users need this"
		    "\n\t\t\t\tin order to use zfs diff");
		break;
	case ZFS_DELEG_NOTE_HOLD:
		str = gettext("Allows adding a user hold to a snapshot");
		break;
	case ZFS_DELEG_NOTE_MOUNT:
		str = gettext("Allows mount/umount of ZFS datasets");
		break;
	case ZFS_DELEG_NOTE_PROMOTE:
		str = gettext("Must also have the 'mount'\n\t\t\t\tand"
		    " 'promote' ability in the origin file system");
		break;
	case ZFS_DELEG_NOTE_RECEIVE:
		str = gettext("Must also have the 'mount' and 'create'"
		    " ability");
		break;
	case ZFS_DELEG_NOTE_RELEASE:
		str = gettext("Allows releasing a user hold which\n\t\t\t\t"
		    "might destroy the snapshot");
		break;
	case ZFS_DELEG_NOTE_RENAME:
		str = gettext("Must also have the 'mount' and 'create'"
		    "\n\t\t\t\tability in the new parent");
		break;
	case ZFS_DELEG_NOTE_ROLLBACK:
		str = gettext("");
		break;
	case ZFS_DELEG_NOTE_SEND:
		str = gettext("");
		break;
	case ZFS_DELEG_NOTE_SHARE:
		str = gettext("Allows sharing file systems over NFS or SMB"
		    "\n\t\t\t\tprotocols");
		break;
	case ZFS_DELEG_NOTE_SNAPSHOT:
		str = gettext("");
		break;
	case ZFS_DELEG_NOTE_LOAD_KEY:
		str = gettext("Allows loading or unloading an encryption key");
		break;
	case ZFS_DELEG_NOTE_CHANGE_KEY:
		str = gettext("Allows changing or adding an encryption key");
		break;
/*
 *	case ZFS_DELEG_NOTE_VSCAN:
 *		str = gettext("");
 *		break;
 */
		/* OTHER */
	case ZFS_DELEG_NOTE_GROUPQUOTA:
		str = gettext("Allows accessing any groupquota@... property");
		break;
	case ZFS_DELEG_NOTE_GROUPUSED:
		str = gettext("Allows reading any groupused@... property");
		break;
	case ZFS_DELEG_NOTE_USERPROP:
		str = gettext("Allows changing any user property");
		break;
	case ZFS_DELEG_NOTE_USERQUOTA:
		str = gettext("Allows accessing any userquota@... property");
		break;
	case ZFS_DELEG_NOTE_USERUSED:
		str = gettext("Allows reading any userused@... property");
		break;
	case ZFS_DELEG_NOTE_USEROBJQUOTA:
		str = gettext("Allows accessing any userobjquota@... property");
		break;
	case ZFS_DELEG_NOTE_GROUPOBJQUOTA:
		str = gettext("Allows accessing any \n\t\t\t\t"
		    "groupobjquota@... property");
		break;
	case ZFS_DELEG_NOTE_GROUPOBJUSED:
		str = gettext("Allows reading any groupobjused@... property");
		break;
	case ZFS_DELEG_NOTE_USEROBJUSED:
		str = gettext("Allows reading any userobjused@... property");
		break;
	case ZFS_DELEG_NOTE_PROJECTQUOTA:
		str = gettext("Allows accessing any projectquota@... property");
		break;
	case ZFS_DELEG_NOTE_PROJECTOBJQUOTA:
		str = gettext("Allows accessing any \n\t\t\t\t"
		    "projectobjquota@... property");
		break;
	case ZFS_DELEG_NOTE_PROJECTUSED:
		str = gettext("Allows reading any projectused@... property");
		break;
	case ZFS_DELEG_NOTE_PROJECTOBJUSED:
		str = gettext("Allows accessing any \n\t\t\t\t"
		    "projectobjused@... property");
		break;
		/* other */
	default:
		str = "";
	}

	return (str);
}

struct allow_opts {
	boolean_t local;
	boolean_t descend;
	boolean_t user;
	boolean_t group;
	boolean_t everyone;
	boolean_t create;
	boolean_t set;
	boolean_t recursive; /* unallow only */
	boolean_t prt_usage;

	boolean_t prt_perms;
	char *who;
	char *perms;
	const char *dataset;
};

static inline int
prop_cmp(const void *a, const void *b)
{
	const char *str1 = *(const char **)a;
	const char *str2 = *(const char **)b;
	return (strcmp(str1, str2));
}

static void
allow_usage(boolean_t un, boolean_t requested, const char *msg)
{
	const char *opt_desc[] = {
		"-h", gettext("show this help message and exit"),
		"-l", gettext("set permission locally"),
		"-d", gettext("set permission for descents"),
		"-u", gettext("set permission for user"),
		"-g", gettext("set permission for group"),
		"-e", gettext("set permission for everyone"),
		"-c", gettext("set create time permission"),
		"-s", gettext("define permission set"),
		/* unallow only */
		"-r", gettext("remove permissions recursively"),
	};
	size_t unallow_size = sizeof (opt_desc) / sizeof (char *);
	size_t allow_size = unallow_size - 2;
	const char *props[ZFS_NUM_PROPS];
	int i;
	size_t count = 0;
	FILE *fp = requested ? stdout : stderr;
	zprop_desc_t *pdtbl = zfs_prop_get_table();
	const char *fmt = gettext("%-16s %-14s\t%s\n");

	(void) fprintf(fp, gettext("Usage: %s\n"), get_usage(un ? HELP_UNALLOW :
	    HELP_ALLOW));
	(void) fprintf(fp, gettext("Options:\n"));
	for (i = 0; i < (un ? unallow_size : allow_size); i += 2) {
		const char *opt = opt_desc[i];
		const char *optdsc = opt_desc[i + 1];
		(void) fprintf(fp, gettext("  %-10s  %s\n"), opt, optdsc);
	}

	(void) fprintf(fp, gettext("\nThe following permissions are "
	    "supported:\n\n"));
	(void) fprintf(fp, fmt, gettext("NAME"), gettext("TYPE"),
	    gettext("NOTES"));
	for (i = 0; i < ZFS_NUM_DELEG_NOTES; i++) {
		const char *perm_name = zfs_deleg_perm_tbl[i].z_perm;
		zfs_deleg_note_t perm_note = zfs_deleg_perm_tbl[i].z_note;
		const char *perm_type = deleg_perm_type(perm_note);
		const char *perm_comment = deleg_perm_comment(perm_note);
		(void) fprintf(fp, fmt, perm_name, perm_type, perm_comment);
	}

	for (i = 0; i < ZFS_NUM_PROPS; i++) {
		zprop_desc_t *pd = &pdtbl[i];
		if (pd->pd_visible != B_TRUE)
			continue;

		if (pd->pd_attr == PROP_READONLY)
			continue;

		props[count++] = pd->pd_name;
	}
	props[count] = NULL;

	qsort(props, count, sizeof (char *), prop_cmp);

	for (i = 0; i < count; i++)
		(void) fprintf(fp, fmt, props[i], gettext("property"), "");

	if (msg != NULL)
		(void) fprintf(fp, gettext("\nzfs: error: %s"), msg);

	exit(requested ? 0 : 2);
}

static inline const char *
munge_args(int argc, char **argv, boolean_t un, size_t expected_argc,
    char **permsp)
{
	if (un && argc == expected_argc - 1)
		*permsp = NULL;
	else if (argc == expected_argc)
		*permsp = argv[argc - 2];
	else
		allow_usage(un, B_FALSE,
		    gettext("wrong number of parameters\n"));

	return (argv[argc - 1]);
}

static void
parse_allow_args(int argc, char **argv, boolean_t un, struct allow_opts *opts)
{
	int uge_sum = opts->user + opts->group + opts->everyone;
	int csuge_sum = opts->create + opts->set + uge_sum;
	int ldcsuge_sum = csuge_sum + opts->local + opts->descend;
	int all_sum = un ? ldcsuge_sum + opts->recursive : ldcsuge_sum;

	if (uge_sum > 1)
		allow_usage(un, B_FALSE,
		    gettext("-u, -g, and -e are mutually exclusive\n"));

	if (opts->prt_usage) {
		if (argc == 0 && all_sum == 0)
			allow_usage(un, B_TRUE, NULL);
		else
			usage(B_FALSE);
	}

	if (opts->set) {
		if (csuge_sum > 1)
			allow_usage(un, B_FALSE,
			    gettext("invalid options combined with -s\n"));

		opts->dataset = munge_args(argc, argv, un, 3, &opts->perms);
		if (argv[0][0] != '@')
			allow_usage(un, B_FALSE,
			    gettext("invalid set name: missing '@' prefix\n"));
		opts->who = argv[0];
	} else if (opts->create) {
		if (ldcsuge_sum > 1)
			allow_usage(un, B_FALSE,
			    gettext("invalid options combined with -c\n"));
		opts->dataset = munge_args(argc, argv, un, 2, &opts->perms);
	} else if (opts->everyone) {
		if (csuge_sum > 1)
			allow_usage(un, B_FALSE,
			    gettext("invalid options combined with -e\n"));
		opts->dataset = munge_args(argc, argv, un, 2, &opts->perms);
	} else if (uge_sum == 0 && argc > 0 && strcmp(argv[0], "everyone")
	    == 0) {
		opts->everyone = B_TRUE;
		argc--;
		argv++;
		opts->dataset = munge_args(argc, argv, un, 2, &opts->perms);
	} else if (argc == 1 && !un) {
		opts->prt_perms = B_TRUE;
		opts->dataset = argv[argc-1];
	} else {
		opts->dataset = munge_args(argc, argv, un, 3, &opts->perms);
		opts->who = argv[0];
	}

	if (!opts->local && !opts->descend) {
		opts->local = B_TRUE;
		opts->descend = B_TRUE;
	}
}

static void
store_allow_perm(zfs_deleg_who_type_t type, boolean_t local, boolean_t descend,
    const char *who, char *perms, nvlist_t *top_nvl)
{
	int i;
	char ld[2] = { '\0', '\0' };
	char who_buf[MAXNAMELEN + 32];
	char base_type = '\0';
	char set_type = '\0';
	nvlist_t *base_nvl = NULL;
	nvlist_t *set_nvl = NULL;
	nvlist_t *nvl;

	if (nvlist_alloc(&base_nvl, NV_UNIQUE_NAME, 0) != 0)
		nomem();
	if (nvlist_alloc(&set_nvl, NV_UNIQUE_NAME, 0) !=  0)
		nomem();

	switch (type) {
	case ZFS_DELEG_NAMED_SET_SETS:
	case ZFS_DELEG_NAMED_SET:
		set_type = ZFS_DELEG_NAMED_SET_SETS;
		base_type = ZFS_DELEG_NAMED_SET;
		ld[0] = ZFS_DELEG_NA;
		break;
	case ZFS_DELEG_CREATE_SETS:
	case ZFS_DELEG_CREATE:
		set_type = ZFS_DELEG_CREATE_SETS;
		base_type = ZFS_DELEG_CREATE;
		ld[0] = ZFS_DELEG_NA;
		break;
	case ZFS_DELEG_USER_SETS:
	case ZFS_DELEG_USER:
		set_type = ZFS_DELEG_USER_SETS;
		base_type = ZFS_DELEG_USER;
		if (local)
			ld[0] = ZFS_DELEG_LOCAL;
		if (descend)
			ld[1] = ZFS_DELEG_DESCENDENT;
		break;
	case ZFS_DELEG_GROUP_SETS:
	case ZFS_DELEG_GROUP:
		set_type = ZFS_DELEG_GROUP_SETS;
		base_type = ZFS_DELEG_GROUP;
		if (local)
			ld[0] = ZFS_DELEG_LOCAL;
		if (descend)
			ld[1] = ZFS_DELEG_DESCENDENT;
		break;
	case ZFS_DELEG_EVERYONE_SETS:
	case ZFS_DELEG_EVERYONE:
		set_type = ZFS_DELEG_EVERYONE_SETS;
		base_type = ZFS_DELEG_EVERYONE;
		if (local)
			ld[0] = ZFS_DELEG_LOCAL;
		if (descend)
			ld[1] = ZFS_DELEG_DESCENDENT;
		break;

	default:
		assert(set_type != '\0' && base_type != '\0');
	}

	if (perms != NULL) {
		char *curr = perms;
		char *end = curr + strlen(perms);

		while (curr < end) {
			char *delim = strchr(curr, ',');
			if (delim == NULL)
				delim = end;
			else
				*delim = '\0';

			if (curr[0] == '@')
				nvl = set_nvl;
			else
				nvl = base_nvl;

			(void) nvlist_add_boolean(nvl, curr);
			if (delim != end)
				*delim = ',';
			curr = delim + 1;
		}

		for (i = 0; i < 2; i++) {
			char locality = ld[i];
			if (locality == 0)
				continue;

			if (!nvlist_empty(base_nvl)) {
				if (who != NULL)
					(void) snprintf(who_buf,
					    sizeof (who_buf), "%c%c$%s",
					    base_type, locality, who);
				else
					(void) snprintf(who_buf,
					    sizeof (who_buf), "%c%c$",
					    base_type, locality);

				(void) nvlist_add_nvlist(top_nvl, who_buf,
				    base_nvl);
			}


			if (!nvlist_empty(set_nvl)) {
				if (who != NULL)
					(void) snprintf(who_buf,
					    sizeof (who_buf), "%c%c$%s",
					    set_type, locality, who);
				else
					(void) snprintf(who_buf,
					    sizeof (who_buf), "%c%c$",
					    set_type, locality);

				(void) nvlist_add_nvlist(top_nvl, who_buf,
				    set_nvl);
			}
		}
	} else {
		for (i = 0; i < 2; i++) {
			char locality = ld[i];
			if (locality == 0)
				continue;

			if (who != NULL)
				(void) snprintf(who_buf, sizeof (who_buf),
				    "%c%c$%s", base_type, locality, who);
			else
				(void) snprintf(who_buf, sizeof (who_buf),
				    "%c%c$", base_type, locality);
			(void) nvlist_add_boolean(top_nvl, who_buf);

			if (who != NULL)
				(void) snprintf(who_buf, sizeof (who_buf),
				    "%c%c$%s", set_type, locality, who);
			else
				(void) snprintf(who_buf, sizeof (who_buf),
				    "%c%c$", set_type, locality);
			(void) nvlist_add_boolean(top_nvl, who_buf);
		}
	}
}

static int
construct_fsacl_list(boolean_t un, struct allow_opts *opts, nvlist_t **nvlp)
{
	if (nvlist_alloc(nvlp, NV_UNIQUE_NAME, 0) != 0)
		nomem();

	if (opts->set) {
		store_allow_perm(ZFS_DELEG_NAMED_SET, opts->local,
		    opts->descend, opts->who, opts->perms, *nvlp);
	} else if (opts->create) {
		store_allow_perm(ZFS_DELEG_CREATE, opts->local,
		    opts->descend, NULL, opts->perms, *nvlp);
	} else if (opts->everyone) {
		store_allow_perm(ZFS_DELEG_EVERYONE, opts->local,
		    opts->descend, NULL, opts->perms, *nvlp);
	} else {
		char *curr = opts->who;
		char *end = curr + strlen(curr);

		while (curr < end) {
			const char *who;
			zfs_deleg_who_type_t who_type = ZFS_DELEG_WHO_UNKNOWN;
			char *endch;
			char *delim = strchr(curr, ',');
			char errbuf[256];
			char id[64];
			struct passwd *p = NULL;
			struct group *g = NULL;

			uid_t rid;
			if (delim == NULL)
				delim = end;
			else
				*delim = '\0';

			rid = (uid_t)strtol(curr, &endch, 0);
			if (opts->user) {
				who_type = ZFS_DELEG_USER;
				if (*endch != '\0')
					p = getpwnam(curr);
				else
					p = getpwuid(rid);

				if (p != NULL)
					rid = p->pw_uid;
				else if (*endch != '\0') {
					(void) snprintf(errbuf, 256, gettext(
					    "invalid user %s\n"), curr);
					allow_usage(un, B_TRUE, errbuf);
				}
			} else if (opts->group) {
				who_type = ZFS_DELEG_GROUP;
				if (*endch != '\0')
					g = getgrnam(curr);
				else
					g = getgrgid(rid);

				if (g != NULL)
					rid = g->gr_gid;
				else if (*endch != '\0') {
					(void) snprintf(errbuf, 256, gettext(
					    "invalid group %s\n"),  curr);
					allow_usage(un, B_TRUE, errbuf);
				}
			} else {
				if (*endch != '\0') {
					p = getpwnam(curr);
				} else {
					p = getpwuid(rid);
				}

				if (p == NULL) {
					if (*endch != '\0') {
						g = getgrnam(curr);
					} else {
						g = getgrgid(rid);
					}
				}

				if (p != NULL) {
					who_type = ZFS_DELEG_USER;
					rid = p->pw_uid;
				} else if (g != NULL) {
					who_type = ZFS_DELEG_GROUP;
					rid = g->gr_gid;
				} else {
					(void) snprintf(errbuf, 256, gettext(
					    "invalid user/group %s\n"), curr);
					allow_usage(un, B_TRUE, errbuf);
				}
			}

			(void) sprintf(id, "%u", rid);
			who = id;

			store_allow_perm(who_type, opts->local,
			    opts->descend, who, opts->perms, *nvlp);
			curr = delim + 1;
		}
	}

	return (0);
}

static void
print_set_creat_perms(uu_avl_t *who_avl)
{
	const char *sc_title[] = {
		gettext("Permission sets:\n"),
		gettext("Create time permissions:\n"),
		NULL
	};
	who_perm_node_t *who_node = NULL;
	int prev_weight = -1;

	for (who_node = uu_avl_first(who_avl); who_node != NULL;
	    who_node = uu_avl_next(who_avl, who_node)) {
		uu_avl_t *avl = who_node->who_perm.who_deleg_perm_avl;
		zfs_deleg_who_type_t who_type = who_node->who_perm.who_type;
		const char *who_name = who_node->who_perm.who_name;
		int weight = who_type2weight(who_type);
		boolean_t first = B_TRUE;
		deleg_perm_node_t *deleg_node;

		if (prev_weight != weight) {
			(void) printf("%s", sc_title[weight]);
			prev_weight = weight;
		}

		if (who_name == NULL || strnlen(who_name, 1) == 0)
			(void) printf("\t");
		else
			(void) printf("\t%s ", who_name);

		for (deleg_node = uu_avl_first(avl); deleg_node != NULL;
		    deleg_node = uu_avl_next(avl, deleg_node)) {
			if (first) {
				(void) printf("%s",
				    deleg_node->dpn_perm.dp_name);
				first = B_FALSE;
			} else
				(void) printf(",%s",
				    deleg_node->dpn_perm.dp_name);
		}

		(void) printf("\n");
	}
}

static void
print_uge_deleg_perms(uu_avl_t *who_avl, boolean_t local, boolean_t descend,
    const char *title)
{
	who_perm_node_t *who_node = NULL;
	boolean_t prt_title = B_TRUE;
	uu_avl_walk_t *walk;

	if ((walk = uu_avl_walk_start(who_avl, UU_WALK_ROBUST)) == NULL)
		nomem();

	while ((who_node = uu_avl_walk_next(walk)) != NULL) {
		const char *who_name = who_node->who_perm.who_name;
		const char *nice_who_name = who_node->who_perm.who_ug_name;
		uu_avl_t *avl = who_node->who_perm.who_deleg_perm_avl;
		zfs_deleg_who_type_t who_type = who_node->who_perm.who_type;
		char delim = ' ';
		deleg_perm_node_t *deleg_node;
		boolean_t prt_who = B_TRUE;

		for (deleg_node = uu_avl_first(avl);
		    deleg_node != NULL;
		    deleg_node = uu_avl_next(avl, deleg_node)) {
			if (local != deleg_node->dpn_perm.dp_local ||
			    descend != deleg_node->dpn_perm.dp_descend)
				continue;

			if (prt_who) {
				const char *who = NULL;
				if (prt_title) {
					prt_title = B_FALSE;
					(void) printf("%s", title);
				}

				switch (who_type) {
				case ZFS_DELEG_USER_SETS:
				case ZFS_DELEG_USER:
					who = gettext("user");
					if (nice_who_name)
						who_name  = nice_who_name;
					break;
				case ZFS_DELEG_GROUP_SETS:
				case ZFS_DELEG_GROUP:
					who = gettext("group");
					if (nice_who_name)
						who_name  = nice_who_name;
					break;
				case ZFS_DELEG_EVERYONE_SETS:
				case ZFS_DELEG_EVERYONE:
					who = gettext("everyone");
					who_name = NULL;
					break;

				default:
					assert(who != NULL);
				}

				prt_who = B_FALSE;
				if (who_name == NULL)
					(void) printf("\t%s", who);
				else
					(void) printf("\t%s %s", who, who_name);
			}

			(void) printf("%c%s", delim,
			    deleg_node->dpn_perm.dp_name);
			delim = ',';
		}

		if (!prt_who)
			(void) printf("\n");
	}

	uu_avl_walk_end(walk);
}

static void
print_fs_perms(fs_perm_set_t *fspset)
{
	fs_perm_node_t *node = NULL;
	char buf[MAXNAMELEN + 32];
	const char *dsname = buf;

	for (node = uu_list_first(fspset->fsps_list); node != NULL;
	    node = uu_list_next(fspset->fsps_list, node)) {
		uu_avl_t *sc_avl = node->fspn_fsperm.fsp_sc_avl;
		uu_avl_t *uge_avl = node->fspn_fsperm.fsp_uge_avl;
		int left = 0;

		(void) snprintf(buf, sizeof (buf),
		    gettext("---- Permissions on %s "),
		    node->fspn_fsperm.fsp_name);
		(void) printf("%s", dsname);
		left = 70 - strlen(buf);
		while (left-- > 0)
			(void) printf("-");
		(void) printf("\n");

		print_set_creat_perms(sc_avl);
		print_uge_deleg_perms(uge_avl, B_TRUE, B_FALSE,
		    gettext("Local permissions:\n"));
		print_uge_deleg_perms(uge_avl, B_FALSE, B_TRUE,
		    gettext("Descendent permissions:\n"));
		print_uge_deleg_perms(uge_avl, B_TRUE, B_TRUE,
		    gettext("Local+Descendent permissions:\n"));
	}
}

static fs_perm_set_t fs_perm_set = { NULL, NULL, NULL, NULL };

struct deleg_perms {
	boolean_t un;
	nvlist_t *nvl;
};

static int
set_deleg_perms(zfs_handle_t *zhp, void *data)
{
	struct deleg_perms *perms = (struct deleg_perms *)data;
	zfs_type_t zfs_type = zfs_get_type(zhp);

	if (zfs_type != ZFS_TYPE_FILESYSTEM && zfs_type != ZFS_TYPE_VOLUME)
		return (0);

	return (zfs_set_fsacl(zhp, perms->un, perms->nvl));
}

static int
zfs_do_allow_unallow_impl(int argc, char **argv, boolean_t un)
{
	zfs_handle_t *zhp;
	nvlist_t *perm_nvl = NULL;
	nvlist_t *update_perm_nvl = NULL;
	int error = 1;
	int c;
	struct allow_opts opts = { 0 };

	const char *optstr = un ? "ldugecsrh" : "ldugecsh";

	/* check opts */
	while ((c = getopt(argc, argv, optstr)) != -1) {
		switch (c) {
		case 'l':
			opts.local = B_TRUE;
			break;
		case 'd':
			opts.descend = B_TRUE;
			break;
		case 'u':
			opts.user = B_TRUE;
			break;
		case 'g':
			opts.group = B_TRUE;
			break;
		case 'e':
			opts.everyone = B_TRUE;
			break;
		case 's':
			opts.set = B_TRUE;
			break;
		case 'c':
			opts.create = B_TRUE;
			break;
		case 'r':
			opts.recursive = B_TRUE;
			break;
		case ':':
			(void) fprintf(stderr, gettext("missing argument for "
			    "'%c' option\n"), optopt);
			usage(B_FALSE);
			break;
		case 'h':
			opts.prt_usage = B_TRUE;
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
	parse_allow_args(argc, argv, un, &opts);

	/* try to open the dataset */
	if ((zhp = zfs_open(g_zfs, opts.dataset, ZFS_TYPE_FILESYSTEM |
	    ZFS_TYPE_VOLUME)) == NULL) {
		(void) fprintf(stderr, "Failed to open dataset: %s\n",
		    opts.dataset);
		return (-1);
	}

	if (zfs_get_fsacl(zhp, &perm_nvl) != 0)
		goto cleanup2;

	fs_perm_set_init(&fs_perm_set);
	if (parse_fs_perm_set(&fs_perm_set, perm_nvl) != 0) {
		(void) fprintf(stderr, "Failed to parse fsacl permissions\n");
		goto cleanup1;
	}

	if (opts.prt_perms)
		print_fs_perms(&fs_perm_set);
	else {
		(void) construct_fsacl_list(un, &opts, &update_perm_nvl);
		if (zfs_set_fsacl(zhp, un, update_perm_nvl) != 0)
			goto cleanup0;

		if (un && opts.recursive) {
			struct deleg_perms data = { un, update_perm_nvl };
			if (zfs_iter_filesystems(zhp, set_deleg_perms,
			    &data) != 0)
				goto cleanup0;
		}
	}

	error = 0;

cleanup0:
	nvlist_free(perm_nvl);
	nvlist_free(update_perm_nvl);
cleanup1:
	fs_perm_set_fini(&fs_perm_set);
cleanup2:
	zfs_close(zhp);

	return (error);
}

static int
zfs_do_allow(int argc, char **argv)
{
	return (zfs_do_allow_unallow_impl(argc, argv, B_FALSE));
}

static int
zfs_do_unallow(int argc, char **argv)
{
	return (zfs_do_allow_unallow_impl(argc, argv, B_TRUE));
}

static int
zfs_do_hold_rele_impl(int argc, char **argv, boolean_t holding)
{
	int errors = 0;
	int i;
	const char *tag;
	boolean_t recursive = B_FALSE;
	const char *opts = holding ? "rt" : "r";
	int c;

	/* check options */
	while ((c = getopt(argc, argv, opts)) != -1) {
		switch (c) {
		case 'r':
			recursive = B_TRUE;
			break;
		case '?':
			(void) fprintf(stderr, gettext("invalid option '%c'\n"),
			    optopt);
			usage(B_FALSE);
		}
	}

	argc -= optind;
	argv += optind;

	/* check number of arguments */
	if (argc < 2)
		usage(B_FALSE);

	tag = argv[0];
	--argc;
	++argv;

	if (holding && tag[0] == '.') {
		/* tags starting with '.' are reserved for libzfs */
		(void) fprintf(stderr, gettext("tag may not start with '.'\n"));
		usage(B_FALSE);
	}

	for (i = 0; i < argc; ++i) {
		zfs_handle_t *zhp;
		char parent[ZFS_MAX_DATASET_NAME_LEN];
		const char *delim;
		char *path = argv[i];

		delim = strchr(path, '@');
		if (delim == NULL) {
			(void) fprintf(stderr,
			    gettext("'%s' is not a snapshot\n"), path);
			++errors;
			continue;
		}
		(void) strncpy(parent, path, delim - path);
		parent[delim - path] = '\0';

		zhp = zfs_open(g_zfs, parent,
		    ZFS_TYPE_FILESYSTEM | ZFS_TYPE_VOLUME);
		if (zhp == NULL) {
			++errors;
			continue;
		}
		if (holding) {
			if (zfs_hold(zhp, delim+1, tag, recursive, -1) != 0)
				++errors;
		} else {
			if (zfs_release(zhp, delim+1, tag, recursive) != 0)
				++errors;
		}
		zfs_close(zhp);
	}

	return (errors != 0);
}

/*
 * zfs hold [-r] [-t] <tag> <snap> ...
 *
 *	-r	Recursively hold
 *
 * Apply a user-hold with the given tag to the list of snapshots.
 */
static int
zfs_do_hold(int argc, char **argv)
{
	return (zfs_do_hold_rele_impl(argc, argv, B_TRUE));
}

/*
 * zfs release [-r] <tag> <snap> ...
 *
 *	-r	Recursively release
 *
 * Release a user-hold with the given tag from the list of snapshots.
 */
static int
zfs_do_release(int argc, char **argv)
{
	return (zfs_do_hold_rele_impl(argc, argv, B_FALSE));
}

typedef struct holds_cbdata {
	boolean_t	cb_recursive;
	const char	*cb_snapname;
	nvlist_t	**cb_nvlp;
	size_t		cb_max_namelen;
	size_t		cb_max_taglen;
} holds_cbdata_t;

#define	STRFTIME_FMT_STR "%a %b %e %H:%M %Y"
#define	DATETIME_BUF_LEN (32)
/*
 *
 */
static void
print_holds(boolean_t scripted, int nwidth, int tagwidth, nvlist_t *nvl)
{
	int i;
	nvpair_t *nvp = NULL;
	char *hdr_cols[] = { "NAME", "TAG", "TIMESTAMP" };
	const char *col;

	if (!scripted) {
		for (i = 0; i < 3; i++) {
			col = gettext(hdr_cols[i]);
			if (i < 2)
				(void) printf("%-*s  ", i ? tagwidth : nwidth,
				    col);
			else
				(void) printf("%s\n", col);
		}
	}

	while ((nvp = nvlist_next_nvpair(nvl, nvp)) != NULL) {
		char *zname = nvpair_name(nvp);
		nvlist_t *nvl2;
		nvpair_t *nvp2 = NULL;
		(void) nvpair_value_nvlist(nvp, &nvl2);
		while ((nvp2 = nvlist_next_nvpair(nvl2, nvp2)) != NULL) {
			char tsbuf[DATETIME_BUF_LEN];
			char *tagname = nvpair_name(nvp2);
			uint64_t val = 0;
			time_t time;
			struct tm t;

			(void) nvpair_value_uint64(nvp2, &val);
			time = (time_t)val;
			(void) localtime_r(&time, &t);
			(void) strftime(tsbuf, DATETIME_BUF_LEN,
			    gettext(STRFTIME_FMT_STR), &t);

			if (scripted) {
				(void) printf("%s\t%s\t%s\n", zname,
				    tagname, tsbuf);
			} else {
				(void) printf("%-*s  %-*s  %s\n", nwidth,
				    zname, tagwidth, tagname, tsbuf);
			}
		}
	}
}

/*
 * Generic callback function to list a dataset or snapshot.
 */
static int
holds_callback(zfs_handle_t *zhp, void *data)
{
	holds_cbdata_t *cbp = data;
	nvlist_t *top_nvl = *cbp->cb_nvlp;
	nvlist_t *nvl = NULL;
	nvpair_t *nvp = NULL;
	const char *zname = zfs_get_name(zhp);
	size_t znamelen = strlen(zname);

	if (cbp->cb_recursive) {
		const char *snapname;
		char *delim  = strchr(zname, '@');
		if (delim == NULL)
			return (0);

		snapname = delim + 1;
		if (strcmp(cbp->cb_snapname, snapname))
			return (0);
	}

	if (zfs_get_holds(zhp, &nvl) != 0)
		return (-1);

	if (znamelen > cbp->cb_max_namelen)
		cbp->cb_max_namelen  = znamelen;

	while ((nvp = nvlist_next_nvpair(nvl, nvp)) != NULL) {
		const char *tag = nvpair_name(nvp);
		size_t taglen = strlen(tag);
		if (taglen > cbp->cb_max_taglen)
			cbp->cb_max_taglen  = taglen;
	}

	return (nvlist_add_nvlist(top_nvl, zname, nvl));
}

/*
 * zfs holds [-rH] <snap> ...
 *
 *	-r	Lists holds that are set on the named snapshots recursively.
 *	-H	Scripted mode; elide headers and separate columns by tabs.
 */
static int
zfs_do_holds(int argc, char **argv)
{
	int errors = 0;
	int c;
	int i;
	boolean_t scripted = B_FALSE;
	boolean_t recursive = B_FALSE;
	const char *opts = "rH";
	nvlist_t *nvl;

	int types = ZFS_TYPE_SNAPSHOT;
	holds_cbdata_t cb = { 0 };

	int limit = 0;
	int ret = 0;
	int flags = 0;

	/* check options */
	while ((c = getopt(argc, argv, opts)) != -1) {
		switch (c) {
		case 'r':
			recursive = B_TRUE;
			break;
		case 'H':
			scripted = B_TRUE;
			break;
		case '?':
			(void) fprintf(stderr, gettext("invalid option '%c'\n"),
			    optopt);
			usage(B_FALSE);
		}
	}

	if (recursive) {
		types |= ZFS_TYPE_FILESYSTEM | ZFS_TYPE_VOLUME;
		flags |= ZFS_ITER_RECURSE;
	}

	argc -= optind;
	argv += optind;

	/* check number of arguments */
	if (argc < 1)
		usage(B_FALSE);

	if (nvlist_alloc(&nvl, NV_UNIQUE_NAME, 0) != 0)
		nomem();

	for (i = 0; i < argc; ++i) {
		char *snapshot = argv[i];
		const char *delim;
		const char *snapname;

		delim = strchr(snapshot, '@');
		if (delim == NULL) {
			(void) fprintf(stderr,
			    gettext("'%s' is not a snapshot\n"), snapshot);
			++errors;
			continue;
		}
		snapname = delim + 1;
		if (recursive)
			snapshot[delim - snapshot] = '\0';

		cb.cb_recursive = recursive;
		cb.cb_snapname = snapname;
		cb.cb_nvlp = &nvl;

		/*
		 *  1. collect holds data, set format options
		 */
		ret = zfs_for_each(1, argv + i, flags, types, NULL, NULL, limit,
		    holds_callback, &cb);
		if (ret != 0)
			++errors;
	}

	/*
	 *  2. print holds data
	 */
	print_holds(scripted, cb.cb_max_namelen, cb.cb_max_taglen, nvl);

	if (nvlist_empty(nvl))
		(void) fprintf(stderr, gettext("no datasets available\n"));

	nvlist_free(nvl);

	return (0 != errors);
}

#define	CHECK_SPINNER 30
#define	SPINNER_TIME 3		/* seconds */
#define	MOUNT_TIME 1		/* seconds */

typedef struct get_all_state {
	boolean_t	ga_verbose;
	get_all_cb_t	*ga_cbp;
} get_all_state_t;

static int
get_one_dataset(zfs_handle_t *zhp, void *data)
{
	static char *spin[] = { "-", "\\", "|", "/" };
	static int spinval = 0;
	static int spincheck = 0;
	static time_t last_spin_time = (time_t)0;
	get_all_state_t *state = data;
	zfs_type_t type = zfs_get_type(zhp);

	if (state->ga_verbose) {
		if (--spincheck < 0) {
			time_t now = time(NULL);
			if (last_spin_time + SPINNER_TIME < now) {
				update_progress(spin[spinval++ % 4]);
				last_spin_time = now;
			}
			spincheck = CHECK_SPINNER;
		}
	}

	/*
	 * Iterate over any nested datasets.
	 */
	if (zfs_iter_filesystems(zhp, get_one_dataset, data) != 0) {
		zfs_close(zhp);
		return (1);
	}

	/*
	 * Skip any datasets whose type does not match.
	 */
	if ((type & ZFS_TYPE_FILESYSTEM) == 0) {
		zfs_close(zhp);
		return (0);
	}
	libzfs_add_handle(state->ga_cbp, zhp);
	assert(state->ga_cbp->cb_used <= state->ga_cbp->cb_alloc);

	return (0);
}

static void
get_all_datasets(get_all_cb_t *cbp, boolean_t verbose)
{
	get_all_state_t state = {
	    .ga_verbose = verbose,
	    .ga_cbp = cbp
	};

	if (verbose)
		set_progress_header(gettext("Reading ZFS config"));
	(void) zfs_iter_root(g_zfs, get_one_dataset, &state);

	if (verbose)
		finish_progress(gettext("done."));
}

/*
 * Generic callback for sharing or mounting filesystems.  Because the code is so
 * similar, we have a common function with an extra parameter to determine which
 * mode we are using.
 */
typedef enum { OP_SHARE, OP_MOUNT } share_mount_op_t;

typedef struct share_mount_state {
	share_mount_op_t	sm_op;
	boolean_t	sm_verbose;
	int	sm_flags;
	char	*sm_options;
	char	*sm_proto; /* only valid for OP_SHARE */
	pthread_mutex_t	sm_lock; /* protects the remaining fields */
	uint_t	sm_total; /* number of filesystems to process */
	uint_t	sm_done; /* number of filesystems processed */
	int	sm_status; /* -1 if any of the share/mount operations failed */
} share_mount_state_t;

/*
 * Share or mount a dataset.
 */
static int
share_mount_one(zfs_handle_t *zhp, int op, int flags, char *protocol,
    boolean_t explicit, const char *options)
{
	char mountpoint[ZFS_MAXPROPLEN];
	char shareopts[ZFS_MAXPROPLEN];
	char smbshareopts[ZFS_MAXPROPLEN];
	const char *cmdname = op == OP_SHARE ? "share" : "mount";
	struct mnttab mnt;
	uint64_t zoned, canmount;
	boolean_t shared_nfs, shared_smb;

	assert(zfs_get_type(zhp) & ZFS_TYPE_FILESYSTEM);

	/*
	 * Check to make sure we can mount/share this dataset.  If we
	 * are in the global zone and the filesystem is exported to a
	 * local zone, or if we are in a local zone and the
	 * filesystem is not exported, then it is an error.
	 */
	zoned = zfs_prop_get_int(zhp, ZFS_PROP_ZONED);

	if (zoned && getzoneid() == GLOBAL_ZONEID) {
		if (!explicit)
			return (0);

		(void) fprintf(stderr, gettext("cannot %s '%s': "
		    "dataset is exported to a local zone\n"), cmdname,
		    zfs_get_name(zhp));
		return (1);

	} else if (!zoned && getzoneid() != GLOBAL_ZONEID) {
		if (!explicit)
			return (0);

		(void) fprintf(stderr, gettext("cannot %s '%s': "
		    "permission denied\n"), cmdname,
		    zfs_get_name(zhp));
		return (1);
	}

	/*
	 * Ignore any filesystems which don't apply to us. This
	 * includes those with a legacy mountpoint, or those with
	 * legacy share options.
	 */
	verify(zfs_prop_get(zhp, ZFS_PROP_MOUNTPOINT, mountpoint,
	    sizeof (mountpoint), NULL, NULL, 0, B_FALSE) == 0);
	verify(zfs_prop_get(zhp, ZFS_PROP_SHARENFS, shareopts,
	    sizeof (shareopts), NULL, NULL, 0, B_FALSE) == 0);
	verify(zfs_prop_get(zhp, ZFS_PROP_SHARESMB, smbshareopts,
	    sizeof (smbshareopts), NULL, NULL, 0, B_FALSE) == 0);

	if (op == OP_SHARE && strcmp(shareopts, "off") == 0 &&
	    strcmp(smbshareopts, "off") == 0) {
		if (!explicit)
			return (0);

		(void) fprintf(stderr, gettext("cannot share '%s': "
		    "legacy share\n"), zfs_get_name(zhp));
		(void) fprintf(stderr, gettext("use exports(5) or "
		    "smb.conf(5) to share this filesystem, or set "
		    "the sharenfs or sharesmb property\n"));
		return (1);
	}

	/*
	 * We cannot share or mount legacy filesystems. If the
	 * shareopts is non-legacy but the mountpoint is legacy, we
	 * treat it as a legacy share.
	 */
	if (strcmp(mountpoint, "legacy") == 0) {
		if (!explicit)
			return (0);

		(void) fprintf(stderr, gettext("cannot %s '%s': "
		    "legacy mountpoint\n"), cmdname, zfs_get_name(zhp));
		(void) fprintf(stderr, gettext("use %s(8) to "
		    "%s this filesystem\n"), cmdname, cmdname);
		return (1);
	}

	if (strcmp(mountpoint, "none") == 0) {
		if (!explicit)
			return (0);

		(void) fprintf(stderr, gettext("cannot %s '%s': no "
		    "mountpoint set\n"), cmdname, zfs_get_name(zhp));
		return (1);
	}

	/*
	 * canmount	explicit	outcome
	 * on		no		pass through
	 * on		yes		pass through
	 * off		no		return 0
	 * off		yes		display error, return 1
	 * noauto	no		return 0
	 * noauto	yes		pass through
	 */
	canmount = zfs_prop_get_int(zhp, ZFS_PROP_CANMOUNT);
	if (canmount == ZFS_CANMOUNT_OFF) {
		if (!explicit)
			return (0);

		(void) fprintf(stderr, gettext("cannot %s '%s': "
		    "'canmount' property is set to 'off'\n"), cmdname,
		    zfs_get_name(zhp));
		return (1);
	} else if (canmount == ZFS_CANMOUNT_NOAUTO && !explicit) {
		/*
		 * When performing a 'zfs mount -a', we skip any mounts for
		 * datasets that have 'noauto' set. Sharing a dataset with
		 * 'noauto' set is only allowed if it's mounted.
		 */
		if (op == OP_MOUNT)
			return (0);
		if (op == OP_SHARE && !zfs_is_mounted(zhp, NULL)) {
			/* also purge it from existing exports */
			zfs_unshareall_bypath(zhp, mountpoint);
			return (0);
		}
	}

	/*
	 * If this filesystem is encrypted and does not have
	 * a loaded key, we can not mount it.
	 */
	if ((flags & MS_CRYPT) == 0 &&
	    zfs_prop_get_int(zhp, ZFS_PROP_ENCRYPTION) != ZIO_CRYPT_OFF &&
	    zfs_prop_get_int(zhp, ZFS_PROP_KEYSTATUS) ==
	    ZFS_KEYSTATUS_UNAVAILABLE) {
		if (!explicit)
			return (0);

		(void) fprintf(stderr, gettext("cannot %s '%s': "
		    "encryption key not loaded\n"), cmdname, zfs_get_name(zhp));
		return (1);
	}

	/*
	 * If this filesystem is inconsistent and has a receive resume
	 * token, we can not mount it.
	 */
	if (zfs_prop_get_int(zhp, ZFS_PROP_INCONSISTENT) &&
	    zfs_prop_get(zhp, ZFS_PROP_RECEIVE_RESUME_TOKEN,
	    NULL, 0, NULL, NULL, 0, B_TRUE) == 0) {
		if (!explicit)
			return (0);

		(void) fprintf(stderr, gettext("cannot %s '%s': "
		    "Contains partially-completed state from "
		    "\"zfs receive -s\", which can be resumed with "
		    "\"zfs send -t\"\n"),
		    cmdname, zfs_get_name(zhp));
		return (1);
	}

	if (zfs_prop_get_int(zhp, ZFS_PROP_REDACTED) && !(flags & MS_FORCE)) {
		if (!explicit)
			return (0);

		(void) fprintf(stderr, gettext("cannot %s '%s': "
		    "Dataset is not complete, was created by receiving "
		    "a redacted zfs send stream.\n"), cmdname,
		    zfs_get_name(zhp));
		return (1);
	}

	/*
	 * At this point, we have verified that the mountpoint and/or
	 * shareopts are appropriate for auto management. If the
	 * filesystem is already mounted or shared, return (failing
	 * for explicit requests); otherwise mount or share the
	 * filesystem.
	 */
	switch (op) {
	case OP_SHARE:

		shared_nfs = zfs_is_shared_nfs(zhp, NULL);
		shared_smb = zfs_is_shared_smb(zhp, NULL);

		if ((shared_nfs && shared_smb) ||
		    (shared_nfs && strcmp(shareopts, "on") == 0 &&
		    strcmp(smbshareopts, "off") == 0) ||
		    (shared_smb && strcmp(smbshareopts, "on") == 0 &&
		    strcmp(shareopts, "off") == 0)) {
			if (!explicit)
				return (0);

			(void) fprintf(stderr, gettext("cannot share "
			    "'%s': filesystem already shared\n"),
			    zfs_get_name(zhp));
			return (1);
		}

		if (!zfs_is_mounted(zhp, NULL) &&
		    zfs_mount(zhp, NULL, flags) != 0)
			return (1);

		if (protocol == NULL) {
			if (zfs_shareall(zhp) != 0)
				return (1);
		} else if (strcmp(protocol, "nfs") == 0) {
			if (zfs_share_nfs(zhp))
				return (1);
		} else if (strcmp(protocol, "smb") == 0) {
			if (zfs_share_smb(zhp))
				return (1);
		} else {
			(void) fprintf(stderr, gettext("cannot share "
			    "'%s': invalid share type '%s' "
			    "specified\n"),
			    zfs_get_name(zhp), protocol);
			return (1);
		}

		break;

	case OP_MOUNT:
		if (options == NULL)
			mnt.mnt_mntopts = "";
		else
			mnt.mnt_mntopts = (char *)options;

		if (!hasmntopt(&mnt, MNTOPT_REMOUNT) &&
		    zfs_is_mounted(zhp, NULL)) {
			if (!explicit)
				return (0);

			(void) fprintf(stderr, gettext("cannot mount "
			    "'%s': filesystem already mounted\n"),
			    zfs_get_name(zhp));
			return (1);
		}

		if (zfs_mount(zhp, options, flags) != 0)
			return (1);
		break;
	}

	return (0);
}

/*
 * Reports progress in the form "(current/total)".  Not thread-safe.
 */
static void
report_mount_progress(int current, int total)
{
	static time_t last_progress_time = 0;
	time_t now = time(NULL);
	char info[32];

	/* display header if we're here for the first time */
	if (current == 1) {
		set_progress_header(gettext("Mounting ZFS filesystems"));
	} else if (current != total && last_progress_time + MOUNT_TIME >= now) {
		/* too soon to report again */
		return;
	}

	last_progress_time = now;

	(void) sprintf(info, "(%d/%d)", current, total);

	if (current == total)
		finish_progress(info);
	else
		update_progress(info);
}

/*
 * zfs_foreach_mountpoint() callback that mounts or shares one filesystem and
 * updates the progress meter.
 */
static int
share_mount_one_cb(zfs_handle_t *zhp, void *arg)
{
	share_mount_state_t *sms = arg;
	int ret;

	ret = share_mount_one(zhp, sms->sm_op, sms->sm_flags, sms->sm_proto,
	    B_FALSE, sms->sm_options);

	pthread_mutex_lock(&sms->sm_lock);
	if (ret != 0)
		sms->sm_status = ret;
	sms->sm_done++;
	if (sms->sm_verbose)
		report_mount_progress(sms->sm_done, sms->sm_total);
	pthread_mutex_unlock(&sms->sm_lock);
	return (ret);
}

static void
append_options(char *mntopts, char *newopts)
{
	int len = strlen(mntopts);

	/* original length plus new string to append plus 1 for the comma */
	if (len + 1 + strlen(newopts) >= MNT_LINE_MAX) {
		(void) fprintf(stderr, gettext("the opts argument for "
		    "'%s' option is too long (more than %d chars)\n"),
		    "-o", MNT_LINE_MAX);
		usage(B_FALSE);
	}

	if (*mntopts)
		mntopts[len++] = ',';

	(void) strcpy(&mntopts[len], newopts);
}

static int
share_mount(int op, int argc, char **argv)
{
	int do_all = 0;
	boolean_t verbose = B_FALSE;
	int c, ret = 0;
	char *options = NULL;
	int flags = 0;

	/* check options */
	while ((c = getopt(argc, argv, op == OP_MOUNT ? ":alvo:Of" : "al"))
	    != -1) {
		switch (c) {
		case 'a':
			do_all = 1;
			break;
		case 'v':
			verbose = B_TRUE;
			break;
		case 'l':
			flags |= MS_CRYPT;
			break;
		case 'o':
			if (*optarg == '\0') {
				(void) fprintf(stderr, gettext("empty mount "
				    "options (-o) specified\n"));
				usage(B_FALSE);
			}

			if (options == NULL)
				options = safe_malloc(MNT_LINE_MAX + 1);

			/* option validation is done later */
			append_options(options, optarg);
			break;
		case 'O':
			flags |= MS_OVERLAY;
			break;
		case 'f':
			flags |= MS_FORCE;
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

	/* check number of arguments */
	if (do_all) {
		char *protocol = NULL;

		if (op == OP_SHARE && argc > 0) {
			if (strcmp(argv[0], "nfs") != 0 &&
			    strcmp(argv[0], "smb") != 0) {
				(void) fprintf(stderr, gettext("share type "
				    "must be 'nfs' or 'smb'\n"));
				usage(B_FALSE);
			}
			protocol = argv[0];
			argc--;
			argv++;
		}

		if (argc != 0) {
			(void) fprintf(stderr, gettext("too many arguments\n"));
			usage(B_FALSE);
		}

		start_progress_timer();
		get_all_cb_t cb = { 0 };
		get_all_datasets(&cb, verbose);

		if (cb.cb_used == 0) {
			if (options != NULL)
				free(options);
			return (0);
		}

		share_mount_state_t share_mount_state = { 0 };
		share_mount_state.sm_op = op;
		share_mount_state.sm_verbose = verbose;
		share_mount_state.sm_flags = flags;
		share_mount_state.sm_options = options;
		share_mount_state.sm_proto = protocol;
		share_mount_state.sm_total = cb.cb_used;
		pthread_mutex_init(&share_mount_state.sm_lock, NULL);

		/*
		 * libshare isn't mt-safe, so only do the operation in parallel
		 * if we're mounting. Additionally, the key-loading option must
		 * be serialized so that we can prompt the user for their keys
		 * in a consistent manner.
		 */
		zfs_foreach_mountpoint(g_zfs, cb.cb_handles, cb.cb_used,
		    share_mount_one_cb, &share_mount_state,
		    op == OP_MOUNT && !(flags & MS_CRYPT));
		zfs_commit_all_shares();

		ret = share_mount_state.sm_status;

		for (int i = 0; i < cb.cb_used; i++)
			zfs_close(cb.cb_handles[i]);
		free(cb.cb_handles);
	} else if (argc == 0) {
		struct mnttab entry;

		if ((op == OP_SHARE) || (options != NULL)) {
			(void) fprintf(stderr, gettext("missing filesystem "
			    "argument (specify -a for all)\n"));
			usage(B_FALSE);
		}

		/*
		 * When mount is given no arguments, go through
		 * /proc/self/mounts and display any active ZFS mounts.
		 * We hide any snapshots, since they are controlled
		 * automatically.
		 */

		/* Reopen MNTTAB to prevent reading stale data from open file */
		if (freopen(MNTTAB, "r", mnttab_file) == NULL) {
			if (options != NULL)
				free(options);
			return (ENOENT);
		}

		while (getmntent(mnttab_file, &entry) == 0) {
			if (strcmp(entry.mnt_fstype, MNTTYPE_ZFS) != 0 ||
			    strchr(entry.mnt_special, '@') != NULL)
				continue;

			(void) printf("%-30s  %s\n", entry.mnt_special,
			    entry.mnt_mountp);
		}

	} else {
		zfs_handle_t *zhp;

		if (argc > 1) {
			(void) fprintf(stderr,
			    gettext("too many arguments\n"));
			usage(B_FALSE);
		}

		if ((zhp = zfs_open(g_zfs, argv[0],
		    ZFS_TYPE_FILESYSTEM)) == NULL) {
			ret = 1;
		} else {
			ret = share_mount_one(zhp, op, flags, NULL, B_TRUE,
			    options);
			zfs_commit_all_shares();
			zfs_close(zhp);
		}
	}

	if (options != NULL)
		free(options);

	return (ret);
}

/*
 * zfs mount -a [nfs]
 * zfs mount filesystem
 *
 * Mount all filesystems, or mount the given filesystem.
 */
static int
zfs_do_mount(int argc, char **argv)
{
	return (share_mount(OP_MOUNT, argc, argv));
}

/*
 * zfs share -a [nfs | smb]
 * zfs share filesystem
 *
 * Share all filesystems, or share the given filesystem.
 */
static int
zfs_do_share(int argc, char **argv)
{
	return (share_mount(OP_SHARE, argc, argv));
}

typedef struct unshare_unmount_node {
	zfs_handle_t	*un_zhp;
	char		*un_mountp;
	uu_avl_node_t	un_avlnode;
} unshare_unmount_node_t;

/* ARGSUSED */
static int
unshare_unmount_compare(const void *larg, const void *rarg, void *unused)
{
	const unshare_unmount_node_t *l = larg;
	const unshare_unmount_node_t *r = rarg;

	return (strcmp(l->un_mountp, r->un_mountp));
}

/*
 * Convenience routine used by zfs_do_umount() and manual_unmount().  Given an
 * absolute path, find the entry /proc/self/mounts, verify that it's a
 * ZFS filesystem, and unmount it appropriately.
 */
static int
unshare_unmount_path(int op, char *path, int flags, boolean_t is_manual)
{
	zfs_handle_t *zhp;
	int ret = 0;
	struct stat64 statbuf;
	struct extmnttab entry;
	const char *cmdname = (op == OP_SHARE) ? "unshare" : "unmount";
	ino_t path_inode;

	/*
	 * Search for the given (major,minor) pair in the mount table.
	 */

	/* Reopen MNTTAB to prevent reading stale data from open file */
	if (freopen(MNTTAB, "r", mnttab_file) == NULL)
		return (ENOENT);

	if (getextmntent(path, &entry, &statbuf) != 0) {
		if (op == OP_SHARE) {
			(void) fprintf(stderr, gettext("cannot %s '%s': not "
			    "currently mounted\n"), cmdname, path);
			return (1);
		}
		(void) fprintf(stderr, gettext("warning: %s not in"
		    "/proc/self/mounts\n"), path);
		if ((ret = umount2(path, flags)) != 0)
			(void) fprintf(stderr, gettext("%s: %s\n"), path,
			    strerror(errno));
		return (ret != 0);
	}
	path_inode = statbuf.st_ino;

	if (strcmp(entry.mnt_fstype, MNTTYPE_ZFS) != 0) {
		(void) fprintf(stderr, gettext("cannot %s '%s': not a ZFS "
		    "filesystem\n"), cmdname, path);
		return (1);
	}

	if ((zhp = zfs_open(g_zfs, entry.mnt_special,
	    ZFS_TYPE_FILESYSTEM)) == NULL)
		return (1);

	ret = 1;
	if (stat64(entry.mnt_mountp, &statbuf) != 0) {
		(void) fprintf(stderr, gettext("cannot %s '%s': %s\n"),
		    cmdname, path, strerror(errno));
		goto out;
	} else if (statbuf.st_ino != path_inode) {
		(void) fprintf(stderr, gettext("cannot "
		    "%s '%s': not a mountpoint\n"), cmdname, path);
		goto out;
	}

	if (op == OP_SHARE) {
		char nfs_mnt_prop[ZFS_MAXPROPLEN];
		char smbshare_prop[ZFS_MAXPROPLEN];

		verify(zfs_prop_get(zhp, ZFS_PROP_SHARENFS, nfs_mnt_prop,
		    sizeof (nfs_mnt_prop), NULL, NULL, 0, B_FALSE) == 0);
		verify(zfs_prop_get(zhp, ZFS_PROP_SHARESMB, smbshare_prop,
		    sizeof (smbshare_prop), NULL, NULL, 0, B_FALSE) == 0);

		if (strcmp(nfs_mnt_prop, "off") == 0 &&
		    strcmp(smbshare_prop, "off") == 0) {
			(void) fprintf(stderr, gettext("cannot unshare "
			    "'%s': legacy share\n"), path);
			(void) fprintf(stderr, gettext("use exportfs(8) "
			    "or smbcontrol(1) to unshare this filesystem\n"));
		} else if (!zfs_is_shared(zhp)) {
			(void) fprintf(stderr, gettext("cannot unshare '%s': "
			    "not currently shared\n"), path);
		} else {
			ret = zfs_unshareall_bypath(zhp, path);
			zfs_commit_all_shares();
		}
	} else {
		char mtpt_prop[ZFS_MAXPROPLEN];

		verify(zfs_prop_get(zhp, ZFS_PROP_MOUNTPOINT, mtpt_prop,
		    sizeof (mtpt_prop), NULL, NULL, 0, B_FALSE) == 0);

		if (is_manual) {
			ret = zfs_unmount(zhp, NULL, flags);
		} else if (strcmp(mtpt_prop, "legacy") == 0) {
			(void) fprintf(stderr, gettext("cannot unmount "
			    "'%s': legacy mountpoint\n"),
			    zfs_get_name(zhp));
			(void) fprintf(stderr, gettext("use umount(8) "
			    "to unmount this filesystem\n"));
		} else {
			ret = zfs_unmountall(zhp, flags);
		}
	}

out:
	zfs_close(zhp);

	return (ret != 0);
}

/*
 * Generic callback for unsharing or unmounting a filesystem.
 */
static int
unshare_unmount(int op, int argc, char **argv)
{
	int do_all = 0;
	int flags = 0;
	int ret = 0;
	int c;
	zfs_handle_t *zhp;
	char nfs_mnt_prop[ZFS_MAXPROPLEN];
	char sharesmb[ZFS_MAXPROPLEN];

	/* check options */
	while ((c = getopt(argc, argv, op == OP_SHARE ? ":a" : "afu")) != -1) {
		switch (c) {
		case 'a':
			do_all = 1;
			break;
		case 'f':
			flags |= MS_FORCE;
			break;
		case 'u':
			flags |= MS_CRYPT;
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

	if (do_all) {
		/*
		 * We could make use of zfs_for_each() to walk all datasets in
		 * the system, but this would be very inefficient, especially
		 * since we would have to linearly search /proc/self/mounts for
		 * each one. Instead, do one pass through /proc/self/mounts
		 * looking for zfs entries and call zfs_unmount() for each one.
		 *
		 * Things get a little tricky if the administrator has created
		 * mountpoints beneath other ZFS filesystems.  In this case, we
		 * have to unmount the deepest filesystems first.  To accomplish
		 * this, we place all the mountpoints in an AVL tree sorted by
		 * the special type (dataset name), and walk the result in
		 * reverse to make sure to get any snapshots first.
		 */
		struct mnttab entry;
		uu_avl_pool_t *pool;
		uu_avl_t *tree = NULL;
		unshare_unmount_node_t *node;
		uu_avl_index_t idx;
		uu_avl_walk_t *walk;
		char *protocol = NULL;

		if (op == OP_SHARE && argc > 0) {
			if (strcmp(argv[0], "nfs") != 0 &&
			    strcmp(argv[0], "smb") != 0) {
				(void) fprintf(stderr, gettext("share type "
				    "must be 'nfs' or 'smb'\n"));
				usage(B_FALSE);
			}
			protocol = argv[0];
			argc--;
			argv++;
		}

		if (argc != 0) {
			(void) fprintf(stderr, gettext("too many arguments\n"));
			usage(B_FALSE);
		}

		if (((pool = uu_avl_pool_create("unmount_pool",
		    sizeof (unshare_unmount_node_t),
		    offsetof(unshare_unmount_node_t, un_avlnode),
		    unshare_unmount_compare, UU_DEFAULT)) == NULL) ||
		    ((tree = uu_avl_create(pool, NULL, UU_DEFAULT)) == NULL))
			nomem();

		/* Reopen MNTTAB to prevent reading stale data from open file */
		if (freopen(MNTTAB, "r", mnttab_file) == NULL)
			return (ENOENT);

		while (getmntent(mnttab_file, &entry) == 0) {

			/* ignore non-ZFS entries */
			if (strcmp(entry.mnt_fstype, MNTTYPE_ZFS) != 0)
				continue;

			/* ignore snapshots */
			if (strchr(entry.mnt_special, '@') != NULL)
				continue;

			if ((zhp = zfs_open(g_zfs, entry.mnt_special,
			    ZFS_TYPE_FILESYSTEM)) == NULL) {
				ret = 1;
				continue;
			}

			/*
			 * Ignore datasets that are excluded/restricted by
			 * parent pool name.
			 */
			if (zpool_skip_pool(zfs_get_pool_name(zhp))) {
				zfs_close(zhp);
				continue;
			}

			switch (op) {
			case OP_SHARE:
				verify(zfs_prop_get(zhp, ZFS_PROP_SHARENFS,
				    nfs_mnt_prop,
				    sizeof (nfs_mnt_prop),
				    NULL, NULL, 0, B_FALSE) == 0);
				if (strcmp(nfs_mnt_prop, "off") != 0)
					break;
				verify(zfs_prop_get(zhp, ZFS_PROP_SHARESMB,
				    nfs_mnt_prop,
				    sizeof (nfs_mnt_prop),
				    NULL, NULL, 0, B_FALSE) == 0);
				if (strcmp(nfs_mnt_prop, "off") == 0)
					continue;
				break;
			case OP_MOUNT:
				/* Ignore legacy mounts */
				verify(zfs_prop_get(zhp, ZFS_PROP_MOUNTPOINT,
				    nfs_mnt_prop,
				    sizeof (nfs_mnt_prop),
				    NULL, NULL, 0, B_FALSE) == 0);
				if (strcmp(nfs_mnt_prop, "legacy") == 0)
					continue;
				/* Ignore canmount=noauto mounts */
				if (zfs_prop_get_int(zhp, ZFS_PROP_CANMOUNT) ==
				    ZFS_CANMOUNT_NOAUTO)
					continue;
				break;
			default:
				break;
			}

			node = safe_malloc(sizeof (unshare_unmount_node_t));
			node->un_zhp = zhp;
			node->un_mountp = safe_strdup(entry.mnt_mountp);

			uu_avl_node_init(node, &node->un_avlnode, pool);

			if (uu_avl_find(tree, node, NULL, &idx) == NULL) {
				uu_avl_insert(tree, node, idx);
			} else {
				zfs_close(node->un_zhp);
				free(node->un_mountp);
				free(node);
			}
		}

		/*
		 * Walk the AVL tree in reverse, unmounting each filesystem and
		 * removing it from the AVL tree in the process.
		 */
		if ((walk = uu_avl_walk_start(tree,
		    UU_WALK_REVERSE | UU_WALK_ROBUST)) == NULL)
			nomem();

		while ((node = uu_avl_walk_next(walk)) != NULL) {
			const char *mntarg = NULL;

			uu_avl_remove(tree, node);
			switch (op) {
			case OP_SHARE:
				if (zfs_unshareall_bytype(node->un_zhp,
				    node->un_mountp, protocol) != 0)
					ret = 1;
				break;

			case OP_MOUNT:
				if (zfs_unmount(node->un_zhp,
				    mntarg, flags) != 0)
					ret = 1;
				break;
			}

			zfs_close(node->un_zhp);
			free(node->un_mountp);
			free(node);
		}

		if (op == OP_SHARE)
			zfs_commit_shares(protocol);

		uu_avl_walk_end(walk);
		uu_avl_destroy(tree);
		uu_avl_pool_destroy(pool);

	} else {
		if (argc != 1) {
			if (argc == 0)
				(void) fprintf(stderr,
				    gettext("missing filesystem argument\n"));
			else
				(void) fprintf(stderr,
				    gettext("too many arguments\n"));
			usage(B_FALSE);
		}

		/*
		 * We have an argument, but it may be a full path or a ZFS
		 * filesystem.  Pass full paths off to unmount_path() (shared by
		 * manual_unmount), otherwise open the filesystem and pass to
		 * zfs_unmount().
		 */
		if (argv[0][0] == '/')
			return (unshare_unmount_path(op, argv[0],
			    flags, B_FALSE));

		if ((zhp = zfs_open(g_zfs, argv[0],
		    ZFS_TYPE_FILESYSTEM)) == NULL)
			return (1);

		verify(zfs_prop_get(zhp, op == OP_SHARE ?
		    ZFS_PROP_SHARENFS : ZFS_PROP_MOUNTPOINT,
		    nfs_mnt_prop, sizeof (nfs_mnt_prop), NULL,
		    NULL, 0, B_FALSE) == 0);

		switch (op) {
		case OP_SHARE:
			verify(zfs_prop_get(zhp, ZFS_PROP_SHARENFS,
			    nfs_mnt_prop,
			    sizeof (nfs_mnt_prop),
			    NULL, NULL, 0, B_FALSE) == 0);
			verify(zfs_prop_get(zhp, ZFS_PROP_SHARESMB,
			    sharesmb, sizeof (sharesmb), NULL, NULL,
			    0, B_FALSE) == 0);

			if (strcmp(nfs_mnt_prop, "off") == 0 &&
			    strcmp(sharesmb, "off") == 0) {
				(void) fprintf(stderr, gettext("cannot "
				    "unshare '%s': legacy share\n"),
				    zfs_get_name(zhp));
				(void) fprintf(stderr, gettext("use "
				    "exports(5) or smb.conf(5) to unshare "
				    "this filesystem\n"));
				ret = 1;
			} else if (!zfs_is_shared(zhp)) {
				(void) fprintf(stderr, gettext("cannot "
				    "unshare '%s': not currently "
				    "shared\n"), zfs_get_name(zhp));
				ret = 1;
			} else if (zfs_unshareall(zhp) != 0) {
				ret = 1;
			}
			break;

		case OP_MOUNT:
			if (strcmp(nfs_mnt_prop, "legacy") == 0) {
				(void) fprintf(stderr, gettext("cannot "
				    "unmount '%s': legacy "
				    "mountpoint\n"), zfs_get_name(zhp));
				(void) fprintf(stderr, gettext("use "
				    "umount(8) to unmount this "
				    "filesystem\n"));
				ret = 1;
			} else if (!zfs_is_mounted(zhp, NULL)) {
				(void) fprintf(stderr, gettext("cannot "
				    "unmount '%s': not currently "
				    "mounted\n"),
				    zfs_get_name(zhp));
				ret = 1;
			} else if (zfs_unmountall(zhp, flags) != 0) {
				ret = 1;
			}
			break;
		}

		zfs_close(zhp);
	}

	return (ret);
}

/*
 * zfs unmount [-fu] -a
 * zfs unmount [-fu] filesystem
 *
 * Unmount all filesystems, or a specific ZFS filesystem.
 */
static int
zfs_do_unmount(int argc, char **argv)
{
	return (unshare_unmount(OP_MOUNT, argc, argv));
}

/*
 * zfs unshare -a
 * zfs unshare filesystem
 *
 * Unshare all filesystems, or a specific ZFS filesystem.
 */
static int
zfs_do_unshare(int argc, char **argv)
{
	return (unshare_unmount(OP_SHARE, argc, argv));
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

static int
zfs_do_diff(int argc, char **argv)
{
	zfs_handle_t *zhp;
	int flags = 0;
	char *tosnap = NULL;
	char *fromsnap = NULL;
	char *atp, *copy;
	int err = 0;
	int c;
	struct sigaction sa;

	while ((c = getopt(argc, argv, "FHth")) != -1) {
		switch (c) {
		case 'F':
			flags |= ZFS_DIFF_CLASSIFY;
			break;
		case 'H':
			flags |= ZFS_DIFF_PARSEABLE;
			break;
		case 't':
			flags |= ZFS_DIFF_TIMESTAMP;
			break;
		case 'h':
			flags |= ZFS_DIFF_NO_MANGLE;
			break;
		default:
			(void) fprintf(stderr,
			    gettext("invalid option '%c'\n"), optopt);
			usage(B_FALSE);
		}
	}

	argc -= optind;
	argv += optind;

	if (argc < 1) {
		(void) fprintf(stderr,
		    gettext("must provide at least one snapshot name\n"));
		usage(B_FALSE);
	}

	if (argc > 2) {
		(void) fprintf(stderr, gettext("too many arguments\n"));
		usage(B_FALSE);
	}

	fromsnap = argv[0];
	tosnap = (argc == 2) ? argv[1] : NULL;

	copy = NULL;
	if (*fromsnap != '@')
		copy = strdup(fromsnap);
	else if (tosnap)
		copy = strdup(tosnap);
	if (copy == NULL)
		usage(B_FALSE);

	if ((atp = strchr(copy, '@')) != NULL)
		*atp = '\0';

	if ((zhp = zfs_open(g_zfs, copy, ZFS_TYPE_FILESYSTEM)) == NULL) {
		free(copy);
		return (1);
	}
	free(copy);

	/*
	 * Ignore SIGPIPE so that the library can give us
	 * information on any failure
	 */
	if (sigemptyset(&sa.sa_mask) == -1) {
		err = errno;
		goto out;
	}
	sa.sa_flags = 0;
	sa.sa_handler = SIG_IGN;
	if (sigaction(SIGPIPE, &sa, NULL) == -1) {
		err = errno;
		goto out;
	}

	err = zfs_show_diffs(zhp, STDOUT_FILENO, fromsnap, tosnap, flags);
out:
	zfs_close(zhp);

	return (err != 0);
}

/*
 * zfs bookmark <fs@source>|<fs#source> <fs#bookmark>
 *
 * Creates a bookmark with the given name from the source snapshot
 * or creates a copy of an existing source bookmark.
 */
static int
zfs_do_bookmark(int argc, char **argv)
{
	char *source, *bookname;
	char expbuf[ZFS_MAX_DATASET_NAME_LEN];
	int source_type;
	nvlist_t *nvl;
	int ret = 0;
	int c;

	/* check options */
	while ((c = getopt(argc, argv, "")) != -1) {
		switch (c) {
		case '?':
			(void) fprintf(stderr,
			    gettext("invalid option '%c'\n"), optopt);
			goto usage;
		}
	}

	argc -= optind;
	argv += optind;

	/* check number of arguments */
	if (argc < 1) {
		(void) fprintf(stderr, gettext("missing source argument\n"));
		goto usage;
	}
	if (argc < 2) {
		(void) fprintf(stderr, gettext("missing bookmark argument\n"));
		goto usage;
	}

	source = argv[0];
	bookname = argv[1];

	if (strchr(source, '@') == NULL && strchr(source, '#') == NULL) {
		(void) fprintf(stderr,
		    gettext("invalid source name '%s': "
		    "must contain a '@' or '#'\n"), source);
		goto usage;
	}
	if (strchr(bookname, '#') == NULL) {
		(void) fprintf(stderr,
		    gettext("invalid bookmark name '%s': "
		    "must contain a '#'\n"), bookname);
		goto usage;
	}

	/*
	 * expand source or bookname to full path:
	 * one of them may be specified as short name
	 */
	{
		char **expand;
		char *source_short, *bookname_short;
		source_short = strpbrk(source, "@#");
		bookname_short = strpbrk(bookname, "#");
		if (source_short == source &&
		    bookname_short == bookname) {
			(void) fprintf(stderr, gettext(
			    "either source or bookmark must be specified as "
			    "full dataset paths"));
			goto usage;
		} else if (source_short != source &&
		    bookname_short != bookname) {
			expand = NULL;
		} else if (source_short != source) {
			strlcpy(expbuf, source, sizeof (expbuf));
			expand = &bookname;
		} else if (bookname_short != bookname) {
			strlcpy(expbuf, bookname, sizeof (expbuf));
			expand = &source;
		} else {
			abort();
		}
		if (expand != NULL) {
			*strpbrk(expbuf, "@#") = '\0'; /* dataset name in buf */
			(void) strlcat(expbuf, *expand, sizeof (expbuf));
			*expand = expbuf;
		}
	}

	/* determine source type */
	switch (*strpbrk(source, "@#")) {
		case '@': source_type = ZFS_TYPE_SNAPSHOT; break;
		case '#': source_type = ZFS_TYPE_BOOKMARK; break;
		default: abort();
	}

	/* test the source exists */
	zfs_handle_t *zhp;
	zhp = zfs_open(g_zfs, source, source_type);
	if (zhp == NULL)
		goto usage;
	zfs_close(zhp);

	nvl = fnvlist_alloc();
	fnvlist_add_string(nvl, bookname, source);
	ret = lzc_bookmark(nvl, NULL);
	fnvlist_free(nvl);

	if (ret != 0) {
		const char *err_msg = NULL;
		char errbuf[1024];

		(void) snprintf(errbuf, sizeof (errbuf),
		    dgettext(TEXT_DOMAIN,
		    "cannot create bookmark '%s'"), bookname);

		switch (ret) {
		case EXDEV:
			err_msg = "bookmark is in a different pool";
			break;
		case ZFS_ERR_BOOKMARK_SOURCE_NOT_ANCESTOR:
			err_msg = "source is not an ancestor of the "
			    "new bookmark's dataset";
			break;
		case EEXIST:
			err_msg = "bookmark exists";
			break;
		case EINVAL:
			err_msg = "invalid argument";
			break;
		case ENOTSUP:
			err_msg = "bookmark feature not enabled";
			break;
		case ENOSPC:
			err_msg = "out of space";
			break;
		case ENOENT:
			err_msg = "dataset does not exist";
			break;
		default:
			(void) zfs_standard_error(g_zfs, ret, errbuf);
			break;
		}
		if (err_msg != NULL) {
			(void) fprintf(stderr, "%s: %s\n", errbuf,
			    dgettext(TEXT_DOMAIN, err_msg));
		}
	}

	return (ret != 0);

usage:
	usage(B_FALSE);
	return (-1);
}

static int
zfs_do_channel_program(int argc, char **argv)
{
	int ret, fd, c;
	char *progbuf, *filename, *poolname;
	size_t progsize, progread;
	nvlist_t *outnvl = NULL;
	uint64_t instrlimit = ZCP_DEFAULT_INSTRLIMIT;
	uint64_t memlimit = ZCP_DEFAULT_MEMLIMIT;
	boolean_t sync_flag = B_TRUE, json_output = B_FALSE;
	zpool_handle_t *zhp;

	/* check options */
	while ((c = getopt(argc, argv, "nt:m:j")) != -1) {
		switch (c) {
		case 't':
		case 'm': {
			uint64_t arg;
			char *endp;

			errno = 0;
			arg = strtoull(optarg, &endp, 0);
			if (errno != 0 || *endp != '\0') {
				(void) fprintf(stderr, gettext(
				    "invalid argument "
				    "'%s': expected integer\n"), optarg);
				goto usage;
			}

			if (c == 't') {
				instrlimit = arg;
			} else {
				ASSERT3U(c, ==, 'm');
				memlimit = arg;
			}
			break;
		}
		case 'n': {
			sync_flag = B_FALSE;
			break;
		}
		case 'j': {
			json_output = B_TRUE;
			break;
		}
		case '?':
			(void) fprintf(stderr, gettext("invalid option '%c'\n"),
			    optopt);
			goto usage;
		}
	}

	argc -= optind;
	argv += optind;

	if (argc < 2) {
		(void) fprintf(stderr,
		    gettext("invalid number of arguments\n"));
		goto usage;
	}

	poolname = argv[0];
	filename = argv[1];
	if (strcmp(filename, "-") == 0) {
		fd = 0;
		filename = "standard input";
	} else if ((fd = open(filename, O_RDONLY)) < 0) {
		(void) fprintf(stderr, gettext("cannot open '%s': %s\n"),
		    filename, strerror(errno));
		return (1);
	}

	if ((zhp = zpool_open(g_zfs, poolname)) == NULL) {
		(void) fprintf(stderr, gettext("cannot open pool '%s'\n"),
		    poolname);
		if (fd != 0)
			(void) close(fd);
		return (1);
	}
	zpool_close(zhp);

	/*
	 * Read in the channel program, expanding the program buffer as
	 * necessary.
	 */
	progread = 0;
	progsize = 1024;
	progbuf = safe_malloc(progsize);
	do {
		ret = read(fd, progbuf + progread, progsize - progread);
		progread += ret;
		if (progread == progsize && ret > 0) {
			progsize *= 2;
			progbuf = safe_realloc(progbuf, progsize);
		}
	} while (ret > 0);

	if (fd != 0)
		(void) close(fd);
	if (ret < 0) {
		free(progbuf);
		(void) fprintf(stderr,
		    gettext("cannot read '%s': %s\n"),
		    filename, strerror(errno));
		return (1);
	}
	progbuf[progread] = '\0';

	/*
	 * Any remaining arguments are passed as arguments to the lua script as
	 * a string array:
	 * {
	 *	"argv" -> [ "arg 1", ... "arg n" ],
	 * }
	 */
	nvlist_t *argnvl = fnvlist_alloc();
	fnvlist_add_string_array(argnvl, ZCP_ARG_CLIARGV, argv + 2, argc - 2);

	if (sync_flag) {
		ret = lzc_channel_program(poolname, progbuf,
		    instrlimit, memlimit, argnvl, &outnvl);
	} else {
		ret = lzc_channel_program_nosync(poolname, progbuf,
		    instrlimit, memlimit, argnvl, &outnvl);
	}

	if (ret != 0) {
		/*
		 * On error, report the error message handed back by lua if one
		 * exists.  Otherwise, generate an appropriate error message,
		 * falling back on strerror() for an unexpected return code.
		 */
		char *errstring = NULL;
		const char *msg = gettext("Channel program execution failed");
		uint64_t instructions = 0;
		if (outnvl != NULL && nvlist_exists(outnvl, ZCP_RET_ERROR)) {
			(void) nvlist_lookup_string(outnvl,
			    ZCP_RET_ERROR, &errstring);
			if (errstring == NULL)
				errstring = strerror(ret);
			if (ret == ETIME) {
				(void) nvlist_lookup_uint64(outnvl,
				    ZCP_ARG_INSTRLIMIT, &instructions);
			}
		} else {
			switch (ret) {
			case EINVAL:
				errstring =
				    "Invalid instruction or memory limit.";
				break;
			case ENOMEM:
				errstring = "Return value too large.";
				break;
			case ENOSPC:
				errstring = "Memory limit exhausted.";
				break;
			case ETIME:
				errstring = "Timed out.";
				break;
			case EPERM:
				errstring = "Permission denied. Channel "
				    "programs must be run as root.";
				break;
			default:
				(void) zfs_standard_error(g_zfs, ret, msg);
			}
		}
		if (errstring != NULL)
			(void) fprintf(stderr, "%s:\n%s\n", msg, errstring);

		if (ret == ETIME && instructions != 0)
			(void) fprintf(stderr,
			    gettext("%llu Lua instructions\n"),
			    (u_longlong_t)instructions);
	} else {
		if (json_output) {
			(void) nvlist_print_json(stdout, outnvl);
		} else if (nvlist_empty(outnvl)) {
			(void) fprintf(stdout, gettext("Channel program fully "
			    "executed and did not produce output.\n"));
		} else {
			(void) fprintf(stdout, gettext("Channel program fully "
			    "executed and produced output:\n"));
			dump_nvlist(outnvl, 4);
		}
	}

	free(progbuf);
	fnvlist_free(outnvl);
	fnvlist_free(argnvl);
	return (ret != 0);

usage:
	usage(B_FALSE);
	return (-1);
}


typedef struct loadkey_cbdata {
	boolean_t cb_loadkey;
	boolean_t cb_recursive;
	boolean_t cb_noop;
	char *cb_keylocation;
	uint64_t cb_numfailed;
	uint64_t cb_numattempted;
} loadkey_cbdata_t;

static int
load_key_callback(zfs_handle_t *zhp, void *data)
{
	int ret;
	boolean_t is_encroot;
	loadkey_cbdata_t *cb = data;
	uint64_t keystatus = zfs_prop_get_int(zhp, ZFS_PROP_KEYSTATUS);

	/*
	 * If we are working recursively, we want to skip loading / unloading
	 * keys for non-encryption roots and datasets whose keys are already
	 * in the desired end-state.
	 */
	if (cb->cb_recursive) {
		ret = zfs_crypto_get_encryption_root(zhp, &is_encroot, NULL);
		if (ret != 0)
			return (ret);
		if (!is_encroot)
			return (0);

		if ((cb->cb_loadkey && keystatus == ZFS_KEYSTATUS_AVAILABLE) ||
		    (!cb->cb_loadkey && keystatus == ZFS_KEYSTATUS_UNAVAILABLE))
			return (0);
	}

	cb->cb_numattempted++;

	if (cb->cb_loadkey)
		ret = zfs_crypto_load_key(zhp, cb->cb_noop, cb->cb_keylocation);
	else
		ret = zfs_crypto_unload_key(zhp);

	if (ret != 0) {
		cb->cb_numfailed++;
		return (ret);
	}

	return (0);
}

static int
load_unload_keys(int argc, char **argv, boolean_t loadkey)
{
	int c, ret = 0, flags = 0;
	boolean_t do_all = B_FALSE;
	loadkey_cbdata_t cb = { 0 };

	cb.cb_loadkey = loadkey;

	while ((c = getopt(argc, argv, "anrL:")) != -1) {
		/* noop and alternate keylocations only apply to zfs load-key */
		if (loadkey) {
			switch (c) {
			case 'n':
				cb.cb_noop = B_TRUE;
				continue;
			case 'L':
				cb.cb_keylocation = optarg;
				continue;
			default:
				break;
			}
		}

		switch (c) {
		case 'a':
			do_all = B_TRUE;
			cb.cb_recursive = B_TRUE;
			break;
		case 'r':
			flags |= ZFS_ITER_RECURSE;
			cb.cb_recursive = B_TRUE;
			break;
		default:
			(void) fprintf(stderr,
			    gettext("invalid option '%c'\n"), optopt);
			usage(B_FALSE);
		}
	}

	argc -= optind;
	argv += optind;

	if (!do_all && argc == 0) {
		(void) fprintf(stderr,
		    gettext("Missing dataset argument or -a option\n"));
		usage(B_FALSE);
	}

	if (do_all && argc != 0) {
		(void) fprintf(stderr,
		    gettext("Cannot specify dataset with -a option\n"));
		usage(B_FALSE);
	}

	if (cb.cb_recursive && cb.cb_keylocation != NULL &&
	    strcmp(cb.cb_keylocation, "prompt") != 0) {
		(void) fprintf(stderr, gettext("alternate keylocation may only "
		    "be 'prompt' with -r or -a\n"));
		usage(B_FALSE);
	}

	ret = zfs_for_each(argc, argv, flags,
	    ZFS_TYPE_FILESYSTEM | ZFS_TYPE_VOLUME, NULL, NULL, 0,
	    load_key_callback, &cb);

	if (cb.cb_noop || (cb.cb_recursive && cb.cb_numattempted != 0)) {
		(void) printf(gettext("%llu / %llu key(s) successfully %s\n"),
		    (u_longlong_t)(cb.cb_numattempted - cb.cb_numfailed),
		    (u_longlong_t)cb.cb_numattempted,
		    loadkey ? (cb.cb_noop ? "verified" : "loaded") :
		    "unloaded");
	}

	if (cb.cb_numfailed != 0)
		ret = -1;

	return (ret);
}

static int
zfs_do_load_key(int argc, char **argv)
{
	return (load_unload_keys(argc, argv, B_TRUE));
}


static int
zfs_do_unload_key(int argc, char **argv)
{
	return (load_unload_keys(argc, argv, B_FALSE));
}

static int
zfs_do_change_key(int argc, char **argv)
{
	int c, ret;
	uint64_t keystatus;
	boolean_t loadkey = B_FALSE, inheritkey = B_FALSE;
	zfs_handle_t *zhp = NULL;
	nvlist_t *props = fnvlist_alloc();

	while ((c = getopt(argc, argv, "lio:")) != -1) {
		switch (c) {
		case 'l':
			loadkey = B_TRUE;
			break;
		case 'i':
			inheritkey = B_TRUE;
			break;
		case 'o':
			if (!parseprop(props, optarg)) {
				nvlist_free(props);
				return (1);
			}
			break;
		default:
			(void) fprintf(stderr,
			    gettext("invalid option '%c'\n"), optopt);
			usage(B_FALSE);
		}
	}

	if (inheritkey && !nvlist_empty(props)) {
		(void) fprintf(stderr,
		    gettext("Properties not allowed for inheriting\n"));
		usage(B_FALSE);
	}

	argc -= optind;
	argv += optind;

	if (argc < 1) {
		(void) fprintf(stderr, gettext("Missing dataset argument\n"));
		usage(B_FALSE);
	}

	if (argc > 1) {
		(void) fprintf(stderr, gettext("Too many arguments\n"));
		usage(B_FALSE);
	}

	zhp = zfs_open(g_zfs, argv[argc - 1],
	    ZFS_TYPE_FILESYSTEM | ZFS_TYPE_VOLUME);
	if (zhp == NULL)
		usage(B_FALSE);

	if (loadkey) {
		keystatus = zfs_prop_get_int(zhp, ZFS_PROP_KEYSTATUS);
		if (keystatus != ZFS_KEYSTATUS_AVAILABLE) {
			ret = zfs_crypto_load_key(zhp, B_FALSE, NULL);
			if (ret != 0) {
				nvlist_free(props);
				zfs_close(zhp);
				return (-1);
			}
		}

		/* refresh the properties so the new keystatus is visible */
		zfs_refresh_properties(zhp);
	}

	ret = zfs_crypto_rewrap(zhp, props, inheritkey);
	if (ret != 0) {
		nvlist_free(props);
		zfs_close(zhp);
		return (-1);
	}

	nvlist_free(props);
	zfs_close(zhp);
	return (0);
}

/*
 * 1) zfs project [-d|-r] <file|directory ...>
 *    List project ID and inherit flag of file(s) or directories.
 *    -d: List the directory itself, not its children.
 *    -r: List subdirectories recursively.
 *
 * 2) zfs project -C [-k] [-r] <file|directory ...>
 *    Clear project inherit flag and/or ID on the file(s) or directories.
 *    -k: Keep the project ID unchanged. If not specified, the project ID
 *	  will be reset as zero.
 *    -r: Clear on subdirectories recursively.
 *
 * 3) zfs project -c [-0] [-d|-r] [-p id] <file|directory ...>
 *    Check project ID and inherit flag on the file(s) or directories,
 *    report the outliers.
 *    -0: Print file name followed by a NUL instead of newline.
 *    -d: Check the directory itself, not its children.
 *    -p: Specify the referenced ID for comparing with the target file(s)
 *	  or directories' project IDs. If not specified, the target (top)
 *	  directory's project ID will be used as the referenced one.
 *    -r: Check subdirectories recursively.
 *
 * 4) zfs project [-p id] [-r] [-s] <file|directory ...>
 *    Set project ID and/or inherit flag on the file(s) or directories.
 *    -p: Set the project ID as the given id.
 *    -r: Set on subdirectories recursively. If not specify "-p" option,
 *	  it will use top-level directory's project ID as the given id,
 *	  then set both project ID and inherit flag on all descendants
 *	  of the top-level directory.
 *    -s: Set project inherit flag.
 */
static int
zfs_do_project(int argc, char **argv)
{
	zfs_project_control_t zpc = {
		.zpc_expected_projid = ZFS_INVALID_PROJID,
		.zpc_op = ZFS_PROJECT_OP_DEFAULT,
		.zpc_dironly = B_FALSE,
		.zpc_keep_projid = B_FALSE,
		.zpc_newline = B_TRUE,
		.zpc_recursive = B_FALSE,
		.zpc_set_flag = B_FALSE,
	};
	int ret = 0, c;

	if (argc < 2)
		usage(B_FALSE);

	while ((c = getopt(argc, argv, "0Ccdkp:rs")) != -1) {
		switch (c) {
		case '0':
			zpc.zpc_newline = B_FALSE;
			break;
		case 'C':
			if (zpc.zpc_op != ZFS_PROJECT_OP_DEFAULT) {
				(void) fprintf(stderr, gettext("cannot "
				    "specify '-C' '-c' '-s' together\n"));
				usage(B_FALSE);
			}

			zpc.zpc_op = ZFS_PROJECT_OP_CLEAR;
			break;
		case 'c':
			if (zpc.zpc_op != ZFS_PROJECT_OP_DEFAULT) {
				(void) fprintf(stderr, gettext("cannot "
				    "specify '-C' '-c' '-s' together\n"));
				usage(B_FALSE);
			}

			zpc.zpc_op = ZFS_PROJECT_OP_CHECK;
			break;
		case 'd':
			zpc.zpc_dironly = B_TRUE;
			/* overwrite "-r" option */
			zpc.zpc_recursive = B_FALSE;
			break;
		case 'k':
			zpc.zpc_keep_projid = B_TRUE;
			break;
		case 'p': {
			char *endptr;

			errno = 0;
			zpc.zpc_expected_projid = strtoull(optarg, &endptr, 0);
			if (errno != 0 || *endptr != '\0') {
				(void) fprintf(stderr,
				    gettext("project ID must be less than "
				    "%u\n"), UINT32_MAX);
				usage(B_FALSE);
			}
			if (zpc.zpc_expected_projid >= UINT32_MAX) {
				(void) fprintf(stderr,
				    gettext("invalid project ID\n"));
				usage(B_FALSE);
			}
			break;
		}
		case 'r':
			zpc.zpc_recursive = B_TRUE;
			/* overwrite "-d" option */
			zpc.zpc_dironly = B_FALSE;
			break;
		case 's':
			if (zpc.zpc_op != ZFS_PROJECT_OP_DEFAULT) {
				(void) fprintf(stderr, gettext("cannot "
				    "specify '-C' '-c' '-s' together\n"));
				usage(B_FALSE);
			}

			zpc.zpc_set_flag = B_TRUE;
			zpc.zpc_op = ZFS_PROJECT_OP_SET;
			break;
		default:
			(void) fprintf(stderr, gettext("invalid option '%c'\n"),
			    optopt);
			usage(B_FALSE);
		}
	}

	if (zpc.zpc_op == ZFS_PROJECT_OP_DEFAULT) {
		if (zpc.zpc_expected_projid != ZFS_INVALID_PROJID)
			zpc.zpc_op = ZFS_PROJECT_OP_SET;
		else
			zpc.zpc_op = ZFS_PROJECT_OP_LIST;
	}

	switch (zpc.zpc_op) {
	case ZFS_PROJECT_OP_LIST:
		if (zpc.zpc_keep_projid) {
			(void) fprintf(stderr,
			    gettext("'-k' is only valid together with '-C'\n"));
			usage(B_FALSE);
		}
		if (!zpc.zpc_newline) {
			(void) fprintf(stderr,
			    gettext("'-0' is only valid together with '-c'\n"));
			usage(B_FALSE);
		}
		break;
	case ZFS_PROJECT_OP_CHECK:
		if (zpc.zpc_keep_projid) {
			(void) fprintf(stderr,
			    gettext("'-k' is only valid together with '-C'\n"));
			usage(B_FALSE);
		}
		break;
	case ZFS_PROJECT_OP_CLEAR:
		if (zpc.zpc_dironly) {
			(void) fprintf(stderr,
			    gettext("'-d' is useless together with '-C'\n"));
			usage(B_FALSE);
		}
		if (!zpc.zpc_newline) {
			(void) fprintf(stderr,
			    gettext("'-0' is only valid together with '-c'\n"));
			usage(B_FALSE);
		}
		if (zpc.zpc_expected_projid != ZFS_INVALID_PROJID) {
			(void) fprintf(stderr,
			    gettext("'-p' is useless together with '-C'\n"));
			usage(B_FALSE);
		}
		break;
	case ZFS_PROJECT_OP_SET:
		if (zpc.zpc_dironly) {
			(void) fprintf(stderr,
			    gettext("'-d' is useless for set project ID and/or "
			    "inherit flag\n"));
			usage(B_FALSE);
		}
		if (zpc.zpc_keep_projid) {
			(void) fprintf(stderr,
			    gettext("'-k' is only valid together with '-C'\n"));
			usage(B_FALSE);
		}
		if (!zpc.zpc_newline) {
			(void) fprintf(stderr,
			    gettext("'-0' is only valid together with '-c'\n"));
			usage(B_FALSE);
		}
		break;
	default:
		ASSERT(0);
		break;
	}

	argv += optind;
	argc -= optind;
	if (argc == 0) {
		(void) fprintf(stderr,
		    gettext("missing file or directory target(s)\n"));
		usage(B_FALSE);
	}

	for (int i = 0; i < argc; i++) {
		int err;

		err = zfs_project_handle(argv[i], &zpc);
		if (err && !ret)
			ret = err;
	}

	return (ret);
}

static int
zfs_do_wait(int argc, char **argv)
{
	boolean_t enabled[ZFS_WAIT_NUM_ACTIVITIES];
	int error, i;
	int c;

	/* By default, wait for all types of activity. */
	for (i = 0; i < ZFS_WAIT_NUM_ACTIVITIES; i++)
		enabled[i] = B_TRUE;

	while ((c = getopt(argc, argv, "t:")) != -1) {
		switch (c) {
		case 't':
		{
			static char *col_subopts[] = { "deleteq", NULL };
			char *value;

			/* Reset activities array */
			bzero(&enabled, sizeof (enabled));
			while (*optarg != '\0') {
				int activity = getsubopt(&optarg, col_subopts,
				    &value);

				if (activity < 0) {
					(void) fprintf(stderr,
					    gettext("invalid activity '%s'\n"),
					    value);
					usage(B_FALSE);
				}

				enabled[activity] = B_TRUE;
			}
			break;
		}
		case '?':
			(void) fprintf(stderr, gettext("invalid option '%c'\n"),
			    optopt);
			usage(B_FALSE);
		}
	}

	argv += optind;
	argc -= optind;
	if (argc < 1) {
		(void) fprintf(stderr, gettext("missing 'filesystem' "
		    "argument\n"));
		usage(B_FALSE);
	}
	if (argc > 1) {
		(void) fprintf(stderr, gettext("too many arguments\n"));
		usage(B_FALSE);
	}

	zfs_handle_t *zhp = zfs_open(g_zfs, argv[0], ZFS_TYPE_FILESYSTEM);
	if (zhp == NULL)
		return (1);

	for (;;) {
		boolean_t missing = B_FALSE;
		boolean_t any_waited = B_FALSE;

		for (int i = 0; i < ZFS_WAIT_NUM_ACTIVITIES; i++) {
			boolean_t waited;

			if (!enabled[i])
				continue;

			error = zfs_wait_status(zhp, i, &missing, &waited);
			if (error != 0 || missing)
				break;

			any_waited = (any_waited || waited);
		}

		if (error != 0 || missing || !any_waited)
			break;
	}

	zfs_close(zhp);

	return (error);
}

/*
 * Display version message
 */
static int
zfs_do_version(int argc, char **argv)
{
	if (zfs_version_print() == -1)
		return (1);

	return (0);
}

int
main(int argc, char **argv)
{
	int ret = 0;
	int i = 0;
	char *cmdname;
	char **newargv;

	(void) setlocale(LC_ALL, "");
	(void) setlocale(LC_NUMERIC, "C");
	(void) textdomain(TEXT_DOMAIN);

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
	 * The 'umount' command is an alias for 'unmount'
	 */
	if (strcmp(cmdname, "umount") == 0)
		cmdname = "unmount";

	/*
	 * The 'recv' command is an alias for 'receive'
	 */
	if (strcmp(cmdname, "recv") == 0)
		cmdname = "receive";

	/*
	 * The 'snap' command is an alias for 'snapshot'
	 */
	if (strcmp(cmdname, "snap") == 0)
		cmdname = "snapshot";

	/*
	 * Special case '-?'
	 */
	if ((strcmp(cmdname, "-?") == 0) ||
	    (strcmp(cmdname, "--help") == 0))
		usage(B_TRUE);

	/*
	 * Special case '-V|--version'
	 */
	if ((strcmp(cmdname, "-V") == 0) || (strcmp(cmdname, "--version") == 0))
		return (zfs_do_version(argc, argv));

	if ((g_zfs = libzfs_init()) == NULL) {
		(void) fprintf(stderr, "%s\n", libzfs_error_init(errno));
		return (1);
	}

	mnttab_file = g_zfs->libzfs_mnttab;

	zfs_save_arguments(argc, argv, history_str, sizeof (history_str));

	libzfs_print_on_error(g_zfs, B_TRUE);

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
	libzfs_mnttab_cache(g_zfs, B_TRUE);
	if (find_command_idx(cmdname, &i) == 0) {
		current_command = &command_table[i];
		ret = command_table[i].func(argc - 1, newargv + 1);
	} else if (strchr(cmdname, '=') != NULL) {
		verify(find_command_idx("set", &i) == 0);
		current_command = &command_table[i];
		ret = command_table[i].func(argc, newargv);
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

#ifdef __FreeBSD__
#include <sys/jail.h>
#include <jail.h>
/*
 * Attach/detach the given dataset to/from the given jail
 */
/* ARGSUSED */
static int
zfs_do_jail_impl(int argc, char **argv, boolean_t attach)
{
	zfs_handle_t *zhp;
	int jailid, ret;

	/* check number of arguments */
	if (argc < 3) {
		(void) fprintf(stderr, gettext("missing argument(s)\n"));
		usage(B_FALSE);
	}
	if (argc > 3) {
		(void) fprintf(stderr, gettext("too many arguments\n"));
		usage(B_FALSE);
	}

	jailid = jail_getid(argv[1]);
	if (jailid < 0) {
		(void) fprintf(stderr, gettext("invalid jail id or name\n"));
		usage(B_FALSE);
	}

	zhp = zfs_open(g_zfs, argv[2], ZFS_TYPE_FILESYSTEM);
	if (zhp == NULL)
		return (1);

	ret = (zfs_jail(zhp, jailid, attach) != 0);

	zfs_close(zhp);
	return (ret);
}

/*
 * zfs jail jailid filesystem
 *
 * Attach the given dataset to the given jail
 */
/* ARGSUSED */
static int
zfs_do_jail(int argc, char **argv)
{
	return (zfs_do_jail_impl(argc, argv, B_TRUE));
}

/*
 * zfs unjail jailid filesystem
 *
 * Detach the given dataset from the given jail
 */
/* ARGSUSED */
static int
zfs_do_unjail(int argc, char **argv)
{
	return (zfs_do_jail_impl(argc, argv, B_FALSE));
}
#endif
