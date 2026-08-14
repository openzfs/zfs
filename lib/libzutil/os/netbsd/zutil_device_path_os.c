// SPDX-License-Identifier: CDDL-1.0
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <paths.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/disklabel.h>
#include <libzutil.h>

/*
 * Known disk-class prefixes.  A leading raw 'r' is only stripped when
 * the remainder is one of these (rwd1a → wd1a, never raid0 → aid0).
 * cd/fd/st stay in this list so identity matching still sees them,
 * but zfs_dev_is_media_wait() keeps them off the import/zdb scan.
 */
static const char *const zfs_disk_classes[] = {
	"wd", "sd", "ld", "dk", "ccd", "vnd", "raid", "cgd",
	"xbd", "fd", "cd", "md", "st", NULL
};

boolean_t
zfs_dev_is_disk_class(const char *leaf)
{
	size_t i, n;

	if (leaf == NULL || leaf[0] == '\0')
		return (B_FALSE);
	for (i = 0; zfs_disk_classes[i] != NULL; i++) {
		n = strlen(zfs_disk_classes[i]);
		if (strncmp(leaf, zfs_disk_classes[i], n) == 0 &&
		    (leaf[n] == '\0' ||
		    (leaf[n] >= '0' && leaf[n] <= '9')))
			return (B_TRUE);
	}
	return (B_FALSE);
}

/*
 * Devices whose open() waits for media.  QEMU always has cd0;
 * opening /dev/rcd0 from zdb -e / zpool import hangs until the
 * 20-minute ZTS kill.  fd/st are the same class of wait.
 */
boolean_t
zfs_dev_is_media_wait(const char *leaf)
{
	static const char *const waiters[] = { "cd", "fd", "st", "ts", NULL };
	const char *p = leaf;
	size_t i, n;

	if (p == NULL || p[0] == '\0')
		return (B_FALSE);
	if (p[0] == 'r' && zfs_dev_is_disk_class(p + 1))
		p++;
	for (i = 0; waiters[i] != NULL; i++) {
		n = strlen(waiters[i]);
		if (strncmp(p, waiters[i], n) == 0 &&
		    (p[n] == '\0' || (p[n] >= '0' && p[n] <= '9')))
			return (B_TRUE);
	}
	return (B_FALSE);
}

/*
 * Whole-unit *block* node for import/zdb probe: ld3, dk0, wd0d.
 * Skip raw (rld3), skip every disklabel letter except RAW_PART
 * on drivers that have no letterless node.  Kernel I/O still
 * opens the raw twin in vdev_disk.c.
 */
boolean_t
zfs_dev_is_import_scan_leaf(const char *name)
{
	const char *p = name;
	size_t n;

	if (p == NULL || p[0] == '\0')
		return (B_FALSE);
	if (p[0] == 'r' && zfs_dev_is_disk_class(p + 1))
		return (B_FALSE);
	if (!zfs_dev_is_disk_class(p))
		return (B_FALSE);
	if (zfs_dev_is_media_wait(p))
		return (B_FALSE);

	n = strlen(p);
	if (n >= 2 && p[n - 1] >= 'a' &&
	    p[n - 1] < 'a' + MAXMAXPARTITIONS &&
	    p[n - 2] >= '0' && p[n - 2] <= '9') {
		if (p[n - 1] != 'a' + RAW_PART)
			return (B_FALSE);
		/* ld(4) / dk(4) already have ld3 / dk0. */
		if ((p[0] == 'l' && p[1] == 'd') ||
		    (p[0] == 'd' && p[1] == 'k'))
			return (B_FALSE);
	}
	return (B_TRUE);
}

/*
 * Character twin of a /dev disk, or of a zvol.
 * ld3 / /dev/ld3 / rld3 / /dev/rld3 → /dev/rld3, and only when
 * that node exists.  A path outside /dev is left alone: ./ld0
 * must not become /dev/rld0.  Zvols are /dev/zvol (block) and
 * /dev/rzvol (raw); dsk and rdsk are symlinks to those.
 */
static boolean_t
zfs_dev_dir_ok(const char *in, const char **basep)
{
	const char *slash;
	size_t dlen;

	slash = strrchr(in, '/');
	if (slash == NULL) {
		*basep = in;
		return (B_TRUE);
	}
	*basep = slash + 1;
	dlen = (size_t)(*basep - in);
	return (dlen == strlen(_PATH_DEV) &&
	    strncmp(in, _PATH_DEV, dlen) == 0);
}

static int
zfs_copy_if_mode(const char *path, char *out, size_t n, mode_t kind)
{
	struct stat st;

	if (strlcpy(out, path, n) >= n)
		return (ENAMETOOLONG);
	if (stat(out, &st) != 0 || (st.st_mode & S_IFMT) != kind)
		return (ENODEV);
	return (0);
}

#define	ZVOL_BLK_PFX	"/dev/zvol/"
#define	ZVOL_RAW_PFX	"/dev/rzvol/"
#define	ZVOL_DSK_PFX	"/dev/zvol/dsk/"
#define	ZVOL_RDSK_PFX	"/dev/zvol/rdsk/"

static int
zfs_zvol_raw_path(const char *in, char *out, size_t n)
{
	const char *rest;
	char raw[MAXPATHLEN];

	if (strncmp(in, ZVOL_RAW_PFX, sizeof (ZVOL_RAW_PFX) - 1) == 0)
		return (zfs_copy_if_mode(in, out, n, S_IFCHR));
	if (strncmp(in, ZVOL_RDSK_PFX, sizeof (ZVOL_RDSK_PFX) - 1) == 0)
		rest = in + sizeof (ZVOL_RDSK_PFX) - 1;
	else if (strncmp(in, ZVOL_DSK_PFX, sizeof (ZVOL_DSK_PFX) - 1) == 0)
		rest = in + sizeof (ZVOL_DSK_PFX) - 1;
	else if (strncmp(in, ZVOL_BLK_PFX, sizeof (ZVOL_BLK_PFX) - 1) == 0) {
		rest = in + sizeof (ZVOL_BLK_PFX) - 1;
		if (strncmp(rest, "dsk/", 4) == 0 ||
		    strncmp(rest, "rdsk/", 5) == 0)
			return (ENODEV);
	} else {
		return (ENODEV);
	}
	if (rest[0] == '\0')
		return (ENODEV);
	if (snprintf(raw, sizeof (raw), ZVOL_RAW_PFX "%s", rest) >=
	    (int)sizeof (raw))
		return (ENAMETOOLONG);
	return (zfs_copy_if_mode(raw, out, n, S_IFCHR));
}

static int
zfs_zvol_block_path(const char *in, char *out, size_t n)
{
	const char *rest;
	char blk[MAXPATHLEN];

	if (strncmp(in, ZVOL_DSK_PFX, sizeof (ZVOL_DSK_PFX) - 1) == 0)
		rest = in + sizeof (ZVOL_DSK_PFX) - 1;
	else if (strncmp(in, ZVOL_RDSK_PFX,
	    sizeof (ZVOL_RDSK_PFX) - 1) == 0)
		rest = in + sizeof (ZVOL_RDSK_PFX) - 1;
	else if (strncmp(in, ZVOL_RAW_PFX, sizeof (ZVOL_RAW_PFX) - 1) == 0)
		rest = in + sizeof (ZVOL_RAW_PFX) - 1;
	else if (strncmp(in, ZVOL_BLK_PFX, sizeof (ZVOL_BLK_PFX) - 1) == 0) {
		rest = in + sizeof (ZVOL_BLK_PFX) - 1;
		if (strncmp(rest, "dsk/", 4) == 0 ||
		    strncmp(rest, "rdsk/", 5) == 0)
			return (ENODEV);
		return (zfs_copy_if_mode(in, out, n, S_IFBLK));
	} else {
		return (ENODEV);
	}
	if (rest[0] == '\0')
		return (ENODEV);
	if (snprintf(blk, sizeof (blk), ZVOL_BLK_PFX "%s", rest) >=
	    (int)sizeof (blk))
		return (ENAMETOOLONG);
	return (zfs_copy_if_mode(blk, out, n, S_IFBLK));
}

static boolean_t
zfs_path_is_zvol(const char *in)
{
	return (strncmp(in, ZVOL_BLK_PFX, sizeof (ZVOL_BLK_PFX) - 1) == 0 ||
	    strncmp(in, ZVOL_RAW_PFX, sizeof (ZVOL_RAW_PFX) - 1) == 0);
}

int
zfs_dev_to_raw_path(const char *in, char *out, size_t n)
{
	const char *base;
	char raw[MAXPATHLEN];

	if (in == NULL || out == NULL || n == 0)
		return (ENODEV);
	if (zfs_path_is_zvol(in))
		return (zfs_zvol_raw_path(in, out, n));
	if (!zfs_dev_dir_ok(in, &base) || base[0] == '\0')
		return (ENODEV);
	if (base[0] == 'r' && zfs_dev_is_disk_class(base + 1)) {
		if (snprintf(raw, sizeof (raw), "%s%s", _PATH_DEV, base) >=
		    (int)sizeof (raw))
			return (ENAMETOOLONG);
	} else if (zfs_dev_is_disk_class(base)) {
		if (snprintf(raw, sizeof (raw), "%sr%s", _PATH_DEV, base) >=
		    (int)sizeof (raw))
			return (ENAMETOOLONG);
	} else {
		return (ENODEV);
	}
	return (zfs_copy_if_mode(raw, out, n, S_IFCHR));
}

/*
 * Userland block twin.  Same directory rule as zfs_dev_to_raw_path.
 * rld3 / /dev/rld3 / ld3 → /dev/ld3 when that node exists.
 */
int
zfs_dev_to_block_path(const char *in, char *out, size_t n)
{
	const char *base;
	char blk[MAXPATHLEN];

	if (in == NULL || out == NULL || n == 0)
		return (ENODEV);
	if (zfs_path_is_zvol(in))
		return (zfs_zvol_block_path(in, out, n));
	if (!zfs_dev_dir_ok(in, &base) || base[0] == '\0')
		return (ENODEV);
	if (base[0] == 'r' && zfs_dev_is_disk_class(base + 1))
		base++;
	else if (!zfs_dev_is_disk_class(base))
		return (ENODEV);
	if (snprintf(blk, sizeof (blk), "%s%s", _PATH_DEV, base) >=
	    (int)sizeof (blk))
		return (ENAMETOOLONG);
	return (zfs_copy_if_mode(blk, out, n, S_IFBLK));
}

/*
 * Map rwd1a / rraid0 / ld2 onto the hw.disknames unit (wd1, raid0, ld2).
 */
boolean_t
zfs_dev_disk_unit(const char *leaf, char *unit, size_t n)
{
	const char *p = leaf;
	char *end;
	size_t i, clen;
	long num;

	if (p == NULL || unit == NULL || n == 0)
		return (B_FALSE);
	if (p[0] == 'r' && zfs_dev_is_disk_class(p + 1))
		p++;
	if (!zfs_dev_is_disk_class(p))
		return (B_FALSE);
	for (i = 0; zfs_disk_classes[i] != NULL; i++) {
		clen = strlen(zfs_disk_classes[i]);
		if (strncmp(p, zfs_disk_classes[i], clen) != 0)
			continue;
		if (p[clen] < '0' || p[clen] > '9')
			return (B_FALSE);
		num = strtol(p + clen, &end, 10);
		if (end == p + clen || num < 0 || num > 1024)
			return (B_FALSE);
		if (snprintf(unit, n, "%s%ld",
		    zfs_disk_classes[i], num) >= (int)n)
			return (B_FALSE);
		return (B_TRUE);
	}
	return (B_FALSE);
}

char *
zfs_strip_partition(const char *dev)
{
	/*
	 * Same no-op as FreeBSD.  Wholedisk is decided by
	 * zfs_dev_is_whole_disk() from the RAW_PART letter, not
	 * by rewriting the path.
	 */
	return (strdup(dev));
}

int
zfs_append_partition(char *path, size_t max_len)
{
	return (strnlen(path, max_len));
}

const char *
zfs_strip_path(const char *path)
{
	if (strncmp(path, _PATH_DEV, sizeof (_PATH_DEV) - 1) == 0)
		return (path + sizeof (_PATH_DEV) - 1);
	return (path);
}

char *
zfs_get_underlying_path(const char *dev_name)
{
	if (dev_name == NULL)
		return (NULL);
	return (realpath(dev_name, NULL));
}

boolean_t
zfs_dev_is_whole_disk(const char *dev_name)
{
	const char *leaf;
	size_t n;
	struct stat st;

	if (stat(dev_name, &st) != 0)
		return (B_FALSE);
	if (!S_ISCHR(st.st_mode) && !S_ISBLK(st.st_mode))
		return (B_FALSE);
	leaf = strrchr(dev_name, '/');
	leaf = (leaf != NULL) ? leaf + 1 : dev_name;
	if (leaf[0] == 'r' && leaf[1] != '\0' &&
	    zfs_dev_is_disk_class(leaf + 1))
		leaf++;
	n = strlen(leaf);
	/*
	 * dk(4) wedges are one volume per minor, not a whole disk.
	 * DISKUNIT would collapse dk0 and dk1.  Same reason
	 * dev_disklabel_class() in zpool_vdev_os.c excludes dk.
	 */
	if (n >= 3 && leaf[0] == 'd' && leaf[1] == 'k' &&
	    leaf[2] >= '0' && leaf[2] <= '9')
		return (B_FALSE);
	/*
	 * Trailing disklabel letter is a slice, not the whole unit —
	 * except RAW_PART ('d' on amd64/i386, 'c' elsewhere), which
	 * is the whole-disk designator.  zpool create tank wd0d must
	 * set wholedisk so autoexpand and the EFI label apply.
	 */
	if (n >= 2 && leaf[n - 1] >= 'a' &&
	    leaf[n - 1] < 'a' + MAXMAXPARTITIONS &&
	    leaf[n - 2] >= '0' && leaf[n - 2] <= '9' &&
	    leaf[n - 1] != 'a' + RAW_PART)
		return (B_FALSE);
	return (B_TRUE);
}

int
zpool_label_disk_wait(const char *path, int timeout_ms)
{
	int settle_ms = 50;
	long sleep_ms = 10;
	hrtime_t start, settle;
	struct stat64 statbuf;

	start = gethrtime();
	settle = 0;
	do {
		errno = 0;
		if ((stat64(path, &statbuf) == 0) && (errno == 0)) {
			if (settle == 0)
				settle = gethrtime();
			else if (NSEC2MSEC(gethrtime() - settle) >= settle_ms)
				return (0);
		} else if (errno != ENOENT) {
			return (errno);
		}
		usleep(sleep_ms * MILLISEC);
	} while (NSEC2MSEC(gethrtime() - start) < timeout_ms);

	return (ENODEV);
}

boolean_t
is_mpath_whole_disk(const char *path)
{
	(void) path;
	return (B_FALSE);
}
