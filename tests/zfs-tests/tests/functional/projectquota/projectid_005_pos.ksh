#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# https://opensource.org/license/CDDL-1.0.
#

. $STF_SUITE/tests/functional/projectquota/projectquota_common.kshlib

#
# DESCRIPTION:
#	A 32-bit program on a 64-bit kernel can read and set a file's
#	project ID and flags (FS_IOC_FSGETXATTR, FS_IOC_FSSETXATTR,
#	FS_IOC32_GETFLAGS, FS_IOC32_SETFLAGS).  These reach the filesystem
#	through its compat_ioctl handler.
#
# STRATEGY:
#	1. Set a project ID and the nodump flag on a file, and a project ID
#	   and the inherit flag on a directory, with chattr.
#	2. Read them back through the 32-bit ioctls (compat_ioctl issues
#	   32-bit system calls from a 64-bit process on x86_64).
#	3. Set another project ID and clear the flags through the 32-bit
#	   ioctls, and check the result with lsattr.
#

function cleanup
{
	log_must rm -f $PRJFILE
	log_must rm -rf $PRJDIR
}

if ! lsattr -pd > /dev/null 2>&1; then
	log_unsupported "Current e2fsprogs does not support set/show project ID"
fi

log_onexit cleanup

log_assert "32-bit programs can read and set project IDs and file flags"

log_must touch $PRJFILE
log_must mkdir $PRJDIR

# 32-bit system calls need x86_64 with ia32 emulation enabled.
compat_ioctl getflags $PRJFILE > /dev/null 2>&1
typeset -i rc=$?
if (( rc == 2 || rc > 128 )); then
	log_unsupported "No 32-bit system calls here (compat_ioctl exit $rc)"
fi

FS_NODUMP_FL=0x40
FS_XFLAG_PROJINHERIT=0x200

log_must chattr +d -p $PRJID1 $PRJFILE
log_must chattr +P -p $PRJID1 $PRJDIR

# FS_IOC32_GETFLAGS: the nodump flag is visible.
typeset flags=$(compat_ioctl getflags $PRJFILE)
log_note "32-bit FS_IOC32_GETFLAGS on the file: $flags"
[[ -n "$flags" ]] && (( (flags & FS_NODUMP_FL) != 0 )) ||
	log_fail "32-bit FS_IOC32_GETFLAGS: got '$flags', expected nodump set"

# FS_IOC_FSGETXATTR: the project IDs, and the inherit flag on the directory.
typeset out=$(compat_ioctl fsgetxattr $PRJFILE)
log_note "32-bit FS_IOC_FSGETXATTR on the file: $out"
[[ "${out#* }" == "$PRJID1" ]] ||
	log_fail "32-bit FS_IOC_FSGETXATTR: got '$out', expected projid $PRJID1"
out=$(compat_ioctl fsgetxattr $PRJDIR)
log_note "32-bit FS_IOC_FSGETXATTR on the directory: $out"
[[ "${out#* }" == "$PRJID1" ]] && (( (${out% *} & FS_XFLAG_PROJINHERIT) != 0 )) ||
	log_fail "32-bit FS_IOC_FSGETXATTR: got '$out', expected projid" \
	    "$PRJID1 and the inherit flag"

# FS_IOC_FSSETXATTR: a new project ID on the file, the inherit flag kept on
# the directory.
log_must compat_ioctl fssetxattr $PRJFILE $PRJID2
log_must eval "lsattr -p $PRJFILE | grep -q '^ *$PRJID2 '"
log_must compat_ioctl fssetxattr $PRJDIR $PRJID2
log_must eval "lsattr -pd $PRJDIR | grep -q '^ *$PRJID2 [^ ]*P'"

# FS_IOC32_SETFLAGS: clear the nodump flag.
log_must compat_ioctl setflags $PRJFILE 0
log_mustnot eval "lsattr $PRJFILE | grep -q '^[^ ]*d'"

# The values survive a remount, so they were stored.
log_must zfs umount $QFS
log_must zfs mount $QFS
log_must eval "lsattr -p $PRJFILE | grep -q '^ *$PRJID2 '"
log_mustnot eval "lsattr $PRJFILE | grep -q '^[^ ]*d'"
log_must eval "lsattr -pd $PRJDIR | grep -q '^ *$PRJID2 [^ ]*P'"

log_pass "32-bit programs can read and set project IDs and file flags"
