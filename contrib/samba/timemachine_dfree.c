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
 * Copyright (c) 2026, Ananda Bhattacharya. All rights reserved.
 */

/*
 * timemachine_dfree.c - Fast ZFS userquota/groupquota dfree helper for Samba
 *
 * Queries ZFS userquota/userused (with automatic fallback to
 * groupquota/groupused) directly via libzfs / libzfs_core (/dev/zfs ioctl)
 * without spawning a shell, and outputs total and available space in
 * 1024-byte blocks for Samba's "dfree command".
 * Runs as the connected SMB user's id.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <pwd.h>
#include <grp.h>
#include <mntent.h>
#include <limits.h>

/* OpenZFS libzfs / libzfs_core C ABI declarations */
typedef struct libzfs_handle libzfs_handle_t;
typedef struct zfs_handle zfs_handle_t;

#define	ZFS_TYPE_FILESYSTEM	(1 << 0)

extern int libzfs_core_init(void);
extern void libzfs_core_fini(void);
extern libzfs_handle_t *libzfs_init(void);
extern void libzfs_fini(libzfs_handle_t *hdl);
extern zfs_handle_t *zfs_open(libzfs_handle_t *hdl, const char *path,
    int types);
extern void zfs_close(zfs_handle_t *zhp);
extern int zfs_prop_get_userquota_int(zfs_handle_t *zhp, const char *propname,
    uint64_t *propvalue);

static void
print_help(const char *prog)
{
	printf("Usage: %s <username> <pool_name>\n\n", prog);
	printf("Queries ZFS userquota and userused (falling back to "
	    "groupquota/groupused)\n");
	printf("for <username> on <pool_name> directly via libzfs/libzfs_core "
	    "(/dev/zfs ioctl)\n");
	printf("and outputs total and available space in 1024-byte blocks for "
	    "Samba's\n");
	printf("'dfree command'.\n\n");
	printf("Parameters:\n");
	printf("  <username>    POSIX username whose ZFS userquota/userused "
	    "to query (e.g., timemachine)\n");
	printf("  <pool_name>   ZFS pool or dataset name (e.g., tank)\n\n");
	printf("Samba Mode:\n");
	printf("  When invoked by Samba as '%s .', automatically resolves "
	    "<username> from\n", prog);
	printf("  geteuid() and <pool_name> from the ZFS mount backing "
	    "current directory '.'.\n\n");
	printf("Options:\n");
	printf("  -h, --help    Show this help message and exit\n\n");
	printf("Example (smb.conf):\n");
	printf("  dfree command = /usr/local/bin/timemachine_dfree\n");
	printf("  dfree cache time = 60\n");
}

/*
 * Resolve the longest-matching ZFS dataset mount backing the current
 * working directory
 */
static int
resolve_zfs_pool_from_cwd(char *pool_buf, size_t buf_len)
{
	char cwd[PATH_MAX];
	if (!getcwd(cwd, sizeof (cwd)))
		return (-1);

	FILE *fp = setmntent("/proc/mounts", "r");
	if (!fp)
		return (-1);

	struct mntent *ent;
	size_t best_len = 0;
	int found = -1;

	while ((ent = getmntent(fp)) != NULL) {
		if (strcmp(ent->mnt_type, "zfs") != 0)
			continue;
		size_t mlen = strlen(ent->mnt_dir);
		if (strncmp(cwd, ent->mnt_dir, mlen) == 0 &&
		    (cwd[mlen] == '\0' || cwd[mlen] == '/' || mlen == 1)) {
			if (mlen >= best_len) {
				best_len = mlen;
				snprintf(pool_buf, buf_len, "%s",
				    ent->mnt_fsname);
				found = 0;
			}
		}
	}
	endmntent(fp);
	return (found);
}

int
main(int argc, char *argv[])
{
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-h") == 0 ||
		    strcmp(argv[i], "--help") == 0) {
			print_help(argv[0]);
			return (0);
		}
	}

	const char *user = NULL;
	const char *pool = NULL;
	char auto_user[128];
	char auto_pool[256];

	/*
	 * Samba expects to run the program with parameters as the executable.
	 * Detect and swap params.
	 * Handle Samba sys_popenv("timemachine_dfree", ".") invocation
	 */
	if (argc == 2 && strcmp(argv[1], ".") == 0) {
		struct passwd *pw = getpwuid(geteuid());
		if (!pw || !pw->pw_name) {
			fprintf(stderr,
			    "Error: Failed to resolve username for UID %u.\n",
			    (unsigned)geteuid());
			return (1);
		}
		snprintf(auto_user, sizeof (auto_user), "%s", pw->pw_name);
		if (resolve_zfs_pool_from_cwd(auto_pool,
		    sizeof (auto_pool)) != 0) {
			fprintf(stderr, "Error: Current directory is not on a "
			    "mounted ZFS dataset.\n");
			return (1);
		}
		user = auto_user;
		pool = auto_pool;
	} else {
		if (argc < 2) {
			fprintf(stderr, "Error: Missing required parameters: "
			    "<username> and <pool_name>.\n");
			fprintf(stderr, "Run '%s --help' for usage "
			    "information.\n", argv[0]);
			return (1);
		}
		if (argc < 3) {
			fprintf(stderr, "Error: Missing required parameter: "
			    "<pool_name> (got username '%s').\n", argv[1]);
			fprintf(stderr, "Run '%s --help' for usage "
			    "information.\n", argv[0]);
			return (1);
		}
		user = argv[1];
		pool = argv[2];
	}

	/* Missing libs to link against */
	if (libzfs_core_init() != 0) {
		fprintf(stderr, "Error: Failed to initialize libzfs_core.\n");
		return (1);
	}

	libzfs_handle_t *lib_hdl = libzfs_init();
	if (!lib_hdl) {
		fprintf(stderr, "Error: Failed to initialize libzfs.\n");
		libzfs_core_fini();
		return (1);
	}

	zfs_handle_t *zhp = zfs_open(lib_hdl, pool, ZFS_TYPE_FILESYSTEM);
	if (!zhp) {
		fprintf(stderr, "Error: Failed to open ZFS dataset '%s'.\n",
		    pool);
		libzfs_fini(lib_hdl);
		libzfs_core_fini();
		return (1);
	}

	char quota_prop[192], used_prop[192];
	snprintf(quota_prop, sizeof (quota_prop), "userquota@%s", user);
	snprintf(used_prop, sizeof (used_prop), "userused@%s", user);

	uint64_t quota = 0, used = 0;
	int rc_q = zfs_prop_get_userquota_int(zhp, quota_prop, &quota);
	int rc_u = zfs_prop_get_userquota_int(zhp, used_prop, &used);

	/* Fallback to primary group quota if userquota is unset (0) */
	if (rc_q != 0 || quota == 0) {
		struct passwd *pw = getpwnam(user);
		if (pw) {
			struct group *gr = getgrgid(pw->pw_gid);
			if (gr && gr->gr_name) {
				snprintf(quota_prop, sizeof (quota_prop),
				    "groupquota@%s", gr->gr_name);
				snprintf(used_prop, sizeof (used_prop),
				    "groupused@%s", gr->gr_name);
				rc_q = zfs_prop_get_userquota_int(zhp,
				    quota_prop, &quota);
				rc_u = zfs_prop_get_userquota_int(zhp,
				    used_prop, &used);
			}
		}
	}

	/* Missing quota for existing username */
	if (rc_q != 0 || rc_u != 0 || quota == 0) {
		fprintf(stderr, "Error: Failed to read valid ZFS "
		    "userquota/groupquota for user '%s' on dataset '%s'.\n",
		    user, pool);
		zfs_close(zhp);
		libzfs_fini(lib_hdl);
		libzfs_core_fini();
		return (1);
	}

	zfs_close(zhp);
	libzfs_fini(lib_hdl);
	libzfs_core_fini();

	/*
	 * Might fail at 2^64 - 1 bytes filesystem; at 2026 prices it might be
	 * US$500 million
	 */
	uint64_t avail = (quota > used) ? (quota - used) : 0;
	printf("%llu %llu 1024\n",
	    (unsigned long long)(quota / 1024ULL),
	    (unsigned long long)(avail / 1024ULL));
	return (0);
}
