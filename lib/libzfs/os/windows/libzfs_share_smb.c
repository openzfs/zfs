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
 * Copyright (c) 2025 Jorgen Lundman <lundman@lundman.net>.
 */

#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <libzfs.h>

#include <windows.h>
#include <lm.h>

#include "libzfs_impl.h"

#pragma comment(lib, "Netapi32.lib")

/*
 * We appear to get 2 kinds of call here, first:
 *
 * smb_enable_share: mountpoint=E:/data/ sharename=NULL
 * smb_enable_share: mountpoint=/BOOM sharename=BOOM
 *
 * This first version is most likely from active mounts on
 * the system, so we will ignore these requests.
 *
 * Second call which has the dataset name, we will call
 * getmntany() to fetch the mountpoint (the Windows
 * version of the mountpoint).
 *
 * Then we will share using the mountpoint, and
 * the sharename with "/" replaced with "_".
 */

// #define VERBOSE

static int
utf8_to_wide(const char *src, wchar_t **dst_out)
{
	int len;
	wchar_t *buf;

	*dst_out = NULL;
	if (!src)
		return (0);

	len = MultiByteToWideChar(CP_UTF8, 0, src, -1, NULL, 0);
	if (len <= 0)
		return (-1);

	buf = malloc(len * sizeof (wchar_t));
	if (!buf)
		return (-1);

	if (MultiByteToWideChar(CP_UTF8, 0, src, -1, buf, len) <= 0) {
		free(buf);
		return (-1);
	}

	*dst_out = buf;
	return (0);
}

static char *
zfsname_to_sharename(const char *zfsname)
{
	char *sharename = strdup(zfsname);
	if (!sharename)
		return (NULL);

	// Replace all "/" with "_"
	for (char *p = sharename; *p; ++p)
		if (*p == '/' || *p == '\\')
			*p = '_';

	return (sharename);
}

/*
 * Given a sharename, find the mountpoint it is mounted at.
 * Will also convert "E:/BOOM/lower" into "E:\BOOM\lower"
 */
static char *
sharename_to_mountpoint(const char *sharename)
{
	FILE *mnttab;
	char *ret = NULL;

	// lets call getmntent to see if it is mounted
	if ((mnttab = fopen(MNTTAB, "re")) == NULL)
		return (NULL);

	struct mnttab srch = { 0 };
	struct mnttab entry;

	srch.mnt_special = (char *)sharename;
	srch.mnt_fstype = (char *)MNTTYPE_ZFS;

	if (getmntany(mnttab, &entry, &srch) == 0) {
		ret = strdup(entry.mnt_mountp);
		if (ret) {
			for (char *p = ret; *p; ++p)
				if (*p == '/')
					*p = '\\';
			// Can not end with a backslash, but must
			// end with one if it is "E:\". Sigh.
			int n = strlen(ret);
			if (n > 3 && ret[n - 1] == '\\')
				ret[n - 1] = '\0';
		}
	}

	fclose(mnttab);

	return (ret);
}

/*
 * Given a sharename, find the mountpoint it is mounted at.
 * Will also change "BOOM/lower" into "BOOM_lower"
 */
static char *
mountpoint_to_sharename(const char *mountpoint)
{
	FILE *mnttab;
	char *ret = NULL;

	// lets call getmntent to see if it is mounted
	if ((mnttab = fopen(MNTTAB, "re")) == NULL)
		return (NULL);

	struct mnttab srch = { 0 };
	struct mnttab entry;

	srch.mnt_mountp = (char *)mountpoint;
	srch.mnt_fstype = (char *)MNTTYPE_ZFS;

	if (getmntany(mnttab, &entry, &srch) == 0) {
		ret = zfsname_to_sharename(entry.mnt_special);
	}

	fclose(mnttab);

	return (ret);
}


/*
 * Enables SMB sharing for the specified share.
 */
static int
smb_enable_share(sa_share_impl_t impl_share)
{
	SHARE_INFO_502 info;
	char *mountpoint = NULL;
	wchar_t *wmountpoint = NULL;
	char *sharename = NULL;
	wchar_t *wsharename = NULL;
	int error = 0;

#ifdef VERBOSE
	fprintf(stderr, "smb_enable_share: mountpoint=%s sharename=%s\n",
	    impl_share->sa_mountpoint ? impl_share->sa_mountpoint : "NULL",
	    impl_share->sa_zfsname ? impl_share->sa_zfsname : "NULL");
#endif

	// Ignore calls without dataset name
	if (!impl_share->sa_zfsname)
		return (SA_OK);

	mountpoint = sharename_to_mountpoint(impl_share->sa_zfsname);

	// Not mounted, nothing to share, probably OK.
	if (!mountpoint)
		return (SA_OK);

	ZeroMemory(&info, sizeof (info));

	// Path from sa_mountpoint
	if (utf8_to_wide(mountpoint, &wmountpoint) != 0) {
		error = SA_NO_MEMORY;
		goto out;
	}

	// Change sharename to valid form.
	sharename = zfsname_to_sharename(impl_share->sa_zfsname);
	if (!sharename) {
		error = SA_NO_MEMORY;
		goto out;
	}

	if (utf8_to_wide(sharename, &wsharename) != 0) {
		error = SA_NO_MEMORY;
		goto out;
	}

	info.shi502_netname = (LPWSTR)wsharename;  // e.g. L"boom_dedup"
	info.shi502_type = STYPE_DISKTREE;
	info.shi502_remark = L"ZFS shared dataset";

	info.shi502_permissions = 0; // ignored for level 502
	info.shi502_max_uses = (DWORD)-1; // unlimited
	info.shi502_current_uses = 0;
	info.shi502_path = (LPWSTR)wmountpoint; // e.g. L"E:\\dedup"
	info.shi502_passwd = NULL;

	info.shi502_reserved = 0;
	// or a proper SD if you want ACLs
	info.shi502_security_descriptor = NULL;

#ifdef VERBOSE
	fprintf(stderr, "smb_enable_share: sharing %ls as %ls\n",
	    info.shi502_path, info.shi502_netname);
#endif

	DWORD parm_err = 0;
	error = NetShareAdd(NULL, 502, (LPBYTE)&info, &parm_err);
	if (error == ERROR_ACCESS_DENIED || error == ERROR_PRIVILEGE_NOT_HELD)
		error = SA_NO_PERMISSION;

out:
	if (wmountpoint)
		free(wmountpoint);
	if (wsharename)
		free(wsharename);
	if (mountpoint)
		free(mountpoint);
	if (sharename)
		free(sharename);

	return (error);
}
/*
 * Disables SMB sharing for the specified share.
 */
static int
smb_disable_share(sa_share_impl_t impl_share)
{
	char *sharename = NULL;
	wchar_t *wsharename = NULL;
	int error = 0;

	if (!impl_share->sa_zfsname)
		sharename = mountpoint_to_sharename(impl_share->sa_mountpoint);
	else
		sharename = zfsname_to_sharename(impl_share->sa_zfsname);

#ifdef VERBOSE
	fprintf(stderr, "smb_disable_share: mountpoint=%s sharename=%s\n",
	    impl_share->sa_mountpoint ? impl_share->sa_mountpoint : "NULL",
	    sharename ? sharename : "NULL");
#endif

	if (!sharename) {
		error = SA_NO_MEMORY;
		goto out;
	}

	if (utf8_to_wide(sharename, &wsharename) != 0) {
		error = SA_NO_MEMORY;
		goto out;
	}

#ifdef VERBOSE
	fprintf(stderr, "smb_disable_share: mountpoint=%s sharename=%s\n",
	    impl_share->sa_mountpoint ? impl_share->sa_mountpoint : "NULL",
	    sharename ? sharename : "NULL");
#endif

	error = NetShareDel(NULL, wsharename, 0);
	if (error == ERROR_ACCESS_DENIED || error == ERROR_PRIVILEGE_NOT_HELD)
		error = SA_NO_PERMISSION;

out:
	if (wsharename)
		free(wsharename);
	if (sharename)
		free(sharename);

	return (error);
}

/*
 * Checks whether the specified SMB share options are syntactically correct.
 */
static int
smb_validate_shareopts(const char *shareopts)
{
	return (0);
}

/*
 * Checks whether a share is currently active.
 */
static boolean_t
smb_is_share_active(sa_share_impl_t impl_share)
{
	DWORD status;
	SHARE_INFO_502 *info = NULL;
	char *sharename = NULL;
	wchar_t *wsharename = NULL;
	boolean_t ret = B_FALSE;

	if (!impl_share->sa_zfsname)
		sharename = mountpoint_to_sharename(impl_share->sa_mountpoint);
	else
		sharename = zfsname_to_sharename(impl_share->sa_zfsname);

#ifdef VERBOSE
	fprintf(stderr, "smb_is_share_active: sharename=%s mountpoint=%s\n",
	    sharename ? sharename : "NULL",
	    impl_share->sa_mountpoint ? impl_share->sa_mountpoint : "NULL");
#endif

	if (!sharename)
		goto out;

	if (utf8_to_wide(sharename, &wsharename) != 0)
		goto out;

	// 2) Query existing share
	status = NetShareGetInfo(NULL, wsharename, 502, (LPBYTE *)&info);

#ifdef VERBOSE
	fprintf(stderr,
	    "smb_is_share_active: mountpoint=%s sharename=%s status=%d\n",
	    impl_share->sa_mountpoint ? impl_share->sa_mountpoint : "NULL",
	    sharename ? sharename : "NULL",
	    status);
#endif

out:
	if (info)
		NetApiBufferFree(info);
	if (wsharename)
		free(wsharename);
	if (sharename)
		free(sharename);

	return (status == NERR_Success);
}

static int
smb_update_shares(void)
{
	return (0);
}

const sa_fstype_t libshare_smb_type = {
	.enable_share = smb_enable_share,
	.disable_share = smb_disable_share,
	.is_shared = smb_is_share_active,

	.validate_shareopts = smb_validate_shareopts,
	.commit_shares = smb_update_shares,
};
