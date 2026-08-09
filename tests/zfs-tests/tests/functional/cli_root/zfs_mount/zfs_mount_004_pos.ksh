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
