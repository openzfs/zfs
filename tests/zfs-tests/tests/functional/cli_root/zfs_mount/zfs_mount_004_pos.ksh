#!/bin/ksh -p
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
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
# Invoke "zfs mount <filesystem>" with a filesystem
# which has been already mounted,
# it will fail with a return code of 1
#
# STRATEGY:
# 1. Make sure that the ZFS filesystem is unmounted.
# 2. Invoke 'zfs mount <filesystem>'.
# 3. Verify that the filesystem is mounted.
# 4. Invoke 'zfs mount <filesystem>' the second times.
# 5. Verify the last mount operation failed with return code of 1.
#

verify_runnable "both"

function cleanup
{
	log_must force_unmount $TESTPOOL/$TESTFS
	return 0
}

typeset -i ret=0

log_assert "Verify that 'zfs $mountcmd <filesystem>' " \
	"with a mounted filesystem will fail with return code 1."

log_onexit cleanup

unmounted $TESTPOOL/$TESTFS || \
	log_must cleanup

log_must zfs $mountcmd $TESTPOOL/$TESTFS

mounted $TESTPOOL/$TESTFS || \
	log_unresolved "Filesystem $TESTPOOL/$TESTFS is unmounted"

zfs $mountcmd $TESTPOOL/$TESTFS
ret=$?
(( ret == 1 )) || \
	log_fail "'zfs $mountcmd $TESTPOOL/$TESTFS' " \
		"unexpected return code of $ret."

log_note "Make sure the filesystem $TESTPOOL/$TESTFS is mounted"
mounted $TESTPOOL/$TESTFS || \
	log_fail Filesystem $TESTPOOL/$TESTFS is unmounted

log_pass "'zfs $mountcmd <filesystem>' with a mounted filesystem " \
	"will fail with return code 1."
