// SPDX-License-Identifier: CDDL-1.0
/*
 * NetBSD mount(8) helper.  mount(8) execs /sbin/mount_zfs with
 *   mount_zfs [-ouFv] special node
 * so the kernel mount path and fstab(5) "zfs" entries work.  The
 * Linux helper is named mount.zfs and talks mtab; this one calls
 * zfs_mount_at() and lets NetBSD's mount(2) update the mount list.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <libzfs.h>
#include <sys/mntent.h>
#include <sys/mount.h>

static void
usage(void)
{
	(void) fprintf(stderr,
	    "usage: mount_zfs [-o options] [-u] special node\n");
	exit(1);
}

int
main(int argc, char **argv)
{
	libzfs_handle_t *hdl;
	zfs_handle_t *zhp;
	char opts[MNT_LINE_MAX] = { '\0' };
	int flags = 0;
	int c;
	const char *special;
	const char *node;
	int error;

	while ((c = getopt(argc, argv, "o:uFv")) != -1) {
		switch (c) {
		case 'o':
			if (opts[0] != '\0')
				(void) strlcat(opts, ",", sizeof (opts));
			(void) strlcat(opts, optarg, sizeof (opts));
			break;
		case 'u':
			flags |= MS_REMOUNT;
			if (opts[0] != '\0')
				(void) strlcat(opts, ",", sizeof (opts));
			(void) strlcat(opts, MNTOPT_REMOUNT, sizeof (opts));
			break;
		case 'F':
			/* fstab path from mount(8); unused. */
			break;
		case 'v':
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;
	if (argc != 2)
		usage();
	special = argv[0];
	node = argv[1];

	/*
	 * mount(8) adds -o rw even for a plain `mount -t zfs`.
	 * That token must not hide readonly=on: zfs_add_options
	 * would skip the property, and the mount would be rw
	 * (zfs_mount_ro_rw legacy default / mount -w).
	 * `zfs mount -o rw` does not come through here, so it
	 * still overrides.  A remount carries "update"; keep rw
	 * so `mount -uw` can undo ro.
	 */
	if (opts[0] != '\0' && strstr(opts, "update") == NULL &&
	    strstr(opts, "remount") == NULL) {
		char filtered[MNT_LINE_MAX];
		char *dup, *s, *tok;
		size_t n = 0;

		filtered[0] = '\0';
		dup = strdup(opts);
		if (dup != NULL) {
			s = dup;
			while ((tok = strsep(&s, ",")) != NULL) {
				if (tok[0] == '\0' || strcmp(tok, "rw") == 0)
					continue;
				if (n != 0 && n + 1 < sizeof (filtered))
					filtered[n++] = ',';
				while (*tok != '\0' &&
				    n + 1 < sizeof (filtered))
					filtered[n++] = *tok++;
			}
			filtered[n] = '\0';
			(void) strlcpy(opts, filtered, sizeof (opts));
			free(dup);
		}
	}

	hdl = libzfs_init();
	if (hdl == NULL) {
		(void) fprintf(stderr, "mount_zfs: %s\n",
		    libzfs_error_init(errno));
		return (1);
	}

	/*
	 * Snapshots are mounted by mount(8) in zfs_mount_remount.
	 * Opening only ZFS_TYPE_FILESYSTEM makes zfs_open reject
	 * "pool/fs@snap".
	 */
	zhp = zfs_open(hdl, special, ZFS_TYPE_FILESYSTEM | ZFS_TYPE_SNAPSHOT);
	if (zhp == NULL) {
		(void) fprintf(stderr,
		    "mount_zfs: cannot open '%s': %s\n",
		    special, libzfs_error_description(hdl));
		libzfs_fini(hdl);
		return (1);
	}

	/*
	 * mountpoint=none is not a mountable dataset.  mount(8) does
	 * not consult the property; Linux mount.zfs refuses it.
	 * zfs_unmount_nested expects `mount -t zfs` of a none dataset
	 * to fail, and a legacy dataset to succeed.  Remount (-u /
	 * update) is left alone.
	 */
	if ((flags & MS_REMOUNT) == 0 &&
	    strstr(opts, "update") == NULL &&
	    zfs_get_type(zhp) == ZFS_TYPE_FILESYSTEM) {
		char mp[ZFS_MAXPROPLEN];

		if (zfs_prop_get(zhp, ZFS_PROP_MOUNTPOINT, mp, sizeof (mp),
		    NULL, NULL, 0, B_FALSE) == 0 &&
		    strcmp(mp, ZFS_MOUNTPOINT_NONE) == 0) {
			(void) fprintf(stderr, "mount_zfs: cannot mount '%s': "
			    "mountpoint is 'none'\n", special);
			zfs_close(zhp);
			libzfs_fini(hdl);
			return (1);
		}
	}

	error = zfs_mount_at(zhp, opts[0] != '\0' ? opts : NULL, flags, node);
	if (error != 0) {
		(void) fprintf(stderr, "mount_zfs: %s\n",
		    libzfs_error_description(hdl));
		zfs_close(zhp);
		libzfs_fini(hdl);
		return (1);
	}

	zfs_close(zhp);
	libzfs_fini(hdl);
	return (0);
}
