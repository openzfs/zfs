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
# Invoke "zfs mount <filesystem>" with a filesystem whose mountpoint property
# is 'legacy' or 'none',
# it will fail with a return code of 1 and issue an error message.
#
# STRATEGY:
# 1. Make sure that the ZFS filesystem is unmounted.
# 2. Mount the filesystem using the various combinations
#	- zfs set mountpoint=legacy <filesystem>
#	- zfs set mountpoint=none <filesystem>
# 3. Verify that mount failed with return code of 1.
#

verify_runnable "both"

function cleanup
{
	log_must zfs set mountpoint=$TESTDIR $TESTPOOL/$TESTFS
	log_must force_unmount $TESTPOOL/$TESTFS
	return 0
}

log_assert "Verify that 'zfs $mountcmd' with a filesystem " \
	"whose mountpoint property is 'legacy' or 'none' " \
	"will fail with return code 1."

log_onexit cleanup

set -A mopt "legacy" "none"

typeset -i ret=0
typeset -i i=0

while (( i < ${#mopt[*]} )); do
	unmounted $TESTPOOL/$TESTFS || \
		log_must cleanup

	log_must zfs set mountpoint=${mopt[i]} $TESTPOOL/$TESTFS

	zfs $mountcmd $TESTPOOL/$TESTFS
	ret=$?
	(( ret == 1)) || \
		log_fail "'zfs $mountcmd $TESTPOOL/$TESTFS' " \
			"unexpected return code of $ret."

	log_note "Make sure the filesystem $TESTPOOL/$TESTFS is unmounted"
	unmounted $TESTPOOL/$TESTFS || \
		log_fail Filesystem $TESTPOOL/$TESTFS is mounted

	((i = i + 1))
done

log_pass "Verify that 'zfs $mountcmd' with a filesystem " \
	"whose mountpoint property is 'legacy' or 'none' " \
	"will fail with return code 1."
