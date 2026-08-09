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

#
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zfs_mount/zfs_mount.kshlib

#
# DESCRIPTION:
# Invoke "zfs mount <filesystem>" with a filesystem but its mountpoint
# is currently in use.  Under Linux this should succeed and is the
# expected behavior, it will fail with a return code of 1 and issue
# an error message on other platforms.
#
# STRATEGY:
# 1. Make sure that the ZFS filesystem is unmounted.
# 2. Apply 'zfs set mountpoint=path <filesystem>'.
# 3. Change directory to that given mountpoint.
# 3. Invoke 'zfs mount <filesystem>'.
# 4. Verify that mount succeeds on Linux and FreeBSD and fails for other
#    platforms.
#

verify_runnable "both"

function cleanup
{
	[[ "$PWD" = "$TESTDIR" ]] && cd -
	log_must zfs set mountpoint=$TESTDIR $TESTPOOL/$TESTFS
	log_must force_unmount $TESTPOOL/$TESTFS
	return 0
}

typeset -i ret=0

log_assert "Verify that 'zfs $mountcmd' with a filesystem " \
	"whose mountpoint is currently in use will fail with return code 1."

log_onexit cleanup

unmounted $TESTPOOL/$TESTFS || \
	log_must cleanup

[[ -d $TESTDIR ]] || \
	log_must mkdir -p $TESTDIR

cd $TESTDIR || \
	log_unresolved "Unable change directory to $TESTDIR"

zfs $mountcmd $TESTPOOL/$TESTFS
ret=$?
if is_linux || is_freebsd; then
	expected=0
else
	expected=1
fi
(( ret == expected )) || \
    log_fail "'zfs $mountcmd $TESTPOOL/$TESTFS' " \
        "unexpected return code of $ret."

log_note "Make sure the filesystem $TESTPOOL/$TESTFS is unmounted"
if is_linux || is_freebsd; then
    mounted $TESTPOOL/$TESTFS || \
        log_fail Filesystem $TESTPOOL/$TESTFS is unmounted
else
    unmounted $TESTPOOL/$TESTFS || \
        log_fail Filesystem $TESTPOOL/$TESTFS is mounted
fi

log_pass "'zfs $mountcmd' with a filesystem " \
	"whose mountpoint is currently in use failed with return code 1."
