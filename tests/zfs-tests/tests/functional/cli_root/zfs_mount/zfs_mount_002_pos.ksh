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
