#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or https://opensource.org/licenses/CDDL-1.0.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
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
