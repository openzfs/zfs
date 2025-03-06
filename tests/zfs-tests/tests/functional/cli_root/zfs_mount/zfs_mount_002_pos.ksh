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
# Invoking "zfs mount <filesystem>" with a filesystem whose name is not in
# "zfs list", will fail with a return code of 1.
#
# STRATEGY:
# 1. Make sure the NONEXISTFSNAME ZFS filesystem is not in 'zfs list'.
# 2. Invoke 'zfs mount <filesystem>'.
# 3. Verify that mount failed with return code of 1.
#

verify_runnable "both"

function cleanup
{
	typeset fs
	for fs in $NONEXISTFSNAME $TESTFS ; do
		log_must force_unmount $TESTPOOL/$fs
	done
}


log_assert "Verify that 'zfs $mountcmd' with a filesystem " \
	"whose name is not in 'zfs list' will fail with return code 1."

log_onexit cleanup

log_note "Make sure the filesystem $TESTPOOL/$NONEXISTFSNAME " \
	"is not in 'zfs list'"
log_mustnot zfs list $TESTPOOL/$NONEXISTFSNAME

typeset -i ret=0
zfs $mountcmd $TESTPOOL/$NONEXISTFSNAME
ret=$?
(( ret == 1 )) || \
	log_fail "'zfs $mountcmd $TESTPOOL/$NONEXISTFSNAME' " \
		"unexpected return code of $ret."

log_note "Make sure the filesystem $TESTPOOL/$NONEXISTFSNAME is unmounted"
unmounted $TESTPOOL/$NONEXISTFSNAME || \
	log_fail Filesystem $TESTPOOL/$NONEXISTFSNAME is mounted

log_pass "'zfs $mountcmd' with a filesystem " \
	"whose name is not in 'zfs list' failed with return code 1."
