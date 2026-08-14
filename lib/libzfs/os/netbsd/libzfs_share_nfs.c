// SPDX-License-Identifier: BSD-2-Clause
/*
 * NetBSD NFS shares via /etc/zfs/exports.
 *
 * NetBSD 11 rc.d/mountd already does, when zfs=YES:
 *	touch /etc/zfs/exports
 *	mountd /etc/exports /etc/zfs/exports
 * This file is the libzfs half: zfs set sharenfs=… rewrites that
 * file (same lock/tmpfile helpers as Linux/FreeBSD) and SIGHUPs
 * mountd.  Option spelling follows NetBSD exports(5).
 * Boot-time export list is NetBSD's rc.d/mountd when zfs=YES  --
 * this tree does not ship an extra rc.d script.
 *
 * Do not compile this file on FreeBSD or Linux.
 */

#include <sys/param.h>
#include <sys/sysctl.h>

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <libzfs.h>
#include "../../libzfs_share.h"

#define	_PATH_MOUNTDPID		"/var/run/mountd.pid"
#define	ZFS_EXPORTS_DIR		"/etc/zfs"
#define	ZFS_EXPORTS_FILE	ZFS_EXPORTS_DIR "/exports"
#define	ZFS_EXPORTS_LOCK	ZFS_EXPORTS_FILE ".lock"

/*
 * Translate sharenfs= opts to exports(5).  Same input shapes as
 * FreeBSD; keywords are the NetBSD 11 set.
 *
 *	ro,network=192.168.0.0,mask=255.255.255.0,maproot=0
 *	→  -ro -network=192.168.0.0 -mask=255.255.255.0 -maproot=0
 */
static int
translate_opts(char *oldopts, FILE *out)
{
	static const char *const known_opts[] = {
	    "ro", "o",
	    "maproot", "mapall", "root", "r",
	    "mask", "m",
	    "network", "n",
	    "alldirs", "public", "webnfs", "index",
	    "noresvport", "noresvmnt", "kerb", "k",
	};
	char *newopts, *o, *s;
	unsigned int i;
	size_t len, newopts_len;
	int ret;

	/*
	 * Worst case: every token gets a leading '-' plus a trailing
	 * space, so 2× is enough.  The old 3/2 factor overflowed on
	 * a string of one-character flags ("o,r,m,n,k").
	 *
	 * Two separator levels: nfs_enable_share_impl splits export
	 * lines on ';', this loop splits one line on comma or space.
	 * Do not add ';' here — that would flatten multi-client
	 * exports into one mountd line.
	 */
	newopts_len = strlen(oldopts) * 2 + 8;
	newopts = malloc(newopts_len);
	if (newopts == NULL)
		return (EOF);
	newopts[0] = '\0';
	s = oldopts;
	while ((o = strsep(&s, ", ")) != NULL) {
		if (o[0] == '-')
			o++;
		if (o[0] == '\0')
			continue;
		/*
		 * rw is the NetBSD exports(5) default.  mountd has no
		 * -rw flag; skip it so sharenfs=rw does not become a
		 * bogus host or a rejected option.
		 */
		if (strcmp(o, "rw") == 0)
			continue;
		for (i = 0; i < __arraycount(known_opts); ++i) {
			len = strlen(known_opts[i]);
			if (strncmp(known_opts[i], o, len) == 0 &&
			    (o[len] == '\0' || o[len] == '=')) {
				strlcat(newopts, "-", newopts_len);
				break;
			}
		}
		strlcat(newopts, o, newopts_len);
		strlcat(newopts, " ", newopts_len);
	}
	ret = fputs(newopts, out);
	free(newopts);
	return (ret);
}

static int
nfs_enable_share_impl(sa_share_impl_t impl_share, FILE *tmpfile)
{
	const char *shareopts = impl_share->sa_shareopts;
	boolean_t need_free, fnd_semi;
	char *mp, *lineopts, *exportopts, *s;
	size_t whitelen;
	int rc;

	if (strcmp(shareopts, "on") == 0)
		shareopts = "";

	rc = nfs_escape_mountpoint(impl_share->sa_mountpoint, &mp, &need_free);
	if (rc != SA_OK)
		return (rc);

	lineopts = strdup(shareopts);
	if (lineopts == NULL) {
		if (need_free)
			free(mp);
		return (SA_SYSTEM_ERR);
	}
	s = lineopts;
	fnd_semi = B_FALSE;
	/* ';' separates export lines; tokens inside a line are comma/space. */
	while ((exportopts = strsep(&s, ";")) != NULL) {
		if (s != NULL)
			fnd_semi = B_TRUE;
		if (fnd_semi) {
			whitelen = strspn(exportopts, "\t ");
			if (exportopts[whitelen] == '\0')
				continue;
		}
		if (fputs(mp, tmpfile) == EOF ||
		    fputc('\t', tmpfile) == EOF ||
		    translate_opts(exportopts, tmpfile) == EOF ||
		    fputc('\n', tmpfile) == EOF) {
			fprintf(stderr, "failed to write to temporary file\n");
			rc = SA_SYSTEM_ERR;
			break;
		}
	}
	free(lineopts);
	if (need_free)
		free(mp);
	return (rc);
}

static int
nfs_enable_share(sa_share_impl_t impl_share)
{
	return (nfs_toggle_share(
	    ZFS_EXPORTS_LOCK, ZFS_EXPORTS_FILE, ZFS_EXPORTS_DIR, impl_share,
	    nfs_enable_share_impl));
}

static int
nfs_disable_share_impl(sa_share_impl_t impl_share, FILE *tmpfile)
{
	(void) impl_share;
	(void) tmpfile;
	return (SA_OK);
}

static int
nfs_disable_share(sa_share_impl_t impl_share)
{
	return (nfs_toggle_share(
	    ZFS_EXPORTS_LOCK, ZFS_EXPORTS_FILE, ZFS_EXPORTS_DIR, impl_share,
	    nfs_disable_share_impl));
}

static boolean_t
nfs_is_shared(sa_share_impl_t impl_share)
{
	return (nfs_is_shared_impl(ZFS_EXPORTS_FILE, impl_share));
}

static int
nfs_validate_shareopts(const char *shareopts)
{
	static const char *const known_opts[] = {
	    "ro", "o", "rw",
	    "maproot", "mapall", "root", "r",
	    "mask", "m",
	    "network", "n",
	    "alldirs", "public", "webnfs", "index",
	    "noresvport", "noresvmnt", "kerb", "k",
	};
	const unsigned char *p;
	char *dup, *s, *o;
	unsigned int i;
	size_t len;

	if (shareopts == NULL || shareopts[0] == '\0')
		return (SA_SYNTAX_ERR);
	/*
	 * sharenfs=on is "export with defaults".  libzfs_dataset accepts
	 * it without calling us; sa_enable_share validates first and
	 * enable_share then maps "on" to an empty option list.
	 * Rejecting it made `zfs share -a` fail (canmount_002_pos).
	 */
	if (strcmp(shareopts, "on") == 0)
		return (SA_OK);
	for (p = (const unsigned char *)shareopts; *p != '\0'; p++) {
		if (iscntrl(*p))
			return (SA_SYNTAX_ERR);
	}

	/*
	 * libzfs_dataset.c already accepts "on"/"off" without calling
	 * us.  Anything else must be a known exports(5) keyword, rw,
	 * or a host/network token.  Bare junk (ON, sss) is a syntax
	 * error -- Linux rejects those; FreeBSD currently does not
	 * and ZTS skips the sharenfs= args there.
	 */
	dup = strdup(shareopts);
	if (dup == NULL)
		return (SA_NO_MEMORY);
	s = dup;
	while ((o = strsep(&s, ", ;")) != NULL) {
		if (o[0] == '-')
			o++;
		if (o[0] == '\0')
			continue;
		for (i = 0; i < __arraycount(known_opts); ++i) {
			len = strlen(known_opts[i]);
			if (strncmp(known_opts[i], o, len) == 0 &&
			    (o[len] == '\0' || o[len] == '='))
				goto next;
		}
		if (strpbrk(o, "./:[]") != NULL ||
		    (o[0] >= '0' && o[0] <= '9'))
			goto next;
		free(dup);
		return (SA_SYNTAX_ERR);
next:
		;
	}
	free(dup);
	return (SA_OK);
}

/*
 * NetBSD has no pidfile_open(3).  mountd writes a plain PID file.
 * Confirm the live process is still mountd before SIGHUP — a stale
 * pidfile after mountd died would otherwise signal a recycled PID.
 */
static int
nfs_commit_shares(void)
{
	FILE *fp;
	char buf[32];
	pid_t pid;
	char *end;
	int mib[6];
	struct kinfo_proc2 kp;
	size_t len;

	fp = fopen(_PATH_MOUNTDPID, "r");
	if (fp == NULL)
		return (SA_OK);
	if (fgets(buf, sizeof (buf), fp) == NULL) {
		(void) fclose(fp);
		return (SA_OK);
	}
	(void) fclose(fp);
	pid = (pid_t)strtol(buf, &end, 10);
	if (pid <= 1 || (*end != '\0' && *end != '\n'))
		return (SA_OK);

	/*
	 * KERN_PROC2 takes op, pid, elem size, and count.
	 * KERN_PROC rejects that tail (namelen must be 2) with
	 * EINVAL, so the comm check never ran and mountd was
	 * not signaled.
	 */
	memset(&kp, 0, sizeof (kp));
	len = sizeof (kp);
	mib[0] = CTL_KERN;
	mib[1] = KERN_PROC2;
	mib[2] = KERN_PROC_PID;
	mib[3] = (int)pid;
	mib[4] = (int)sizeof (struct kinfo_proc2);
	mib[5] = 1;
	if (sysctl(mib, 6, &kp, &len, NULL, 0) != 0 || len < sizeof (kp))
		return (SA_OK);
	if (strcmp(kp.p_comm, "mountd") != 0)
		return (SA_OK);
	(void) kill(pid, SIGHUP);
	return (SA_OK);
}

static void
nfs_truncate_shares(void)
{
	nfs_reset_shares(ZFS_EXPORTS_LOCK, ZFS_EXPORTS_FILE);
}

const sa_fstype_t libshare_nfs_type = {
	.enable_share = nfs_enable_share,
	.disable_share = nfs_disable_share,
	.is_shared = nfs_is_shared,
	.validate_shareopts = nfs_validate_shareopts,
	.commit_shares = nfs_commit_shares,
	.truncate_shares = nfs_truncate_shares,
};
