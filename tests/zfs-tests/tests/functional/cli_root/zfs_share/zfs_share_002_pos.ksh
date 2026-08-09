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
# Copyright (c) 2012, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zfs_share/zfs_share.cfg

#
# DESCRIPTION:
# Verify that "zfs share" with a non-existent file system fails.
#
# STRATEGY:
# 1. Make sure the NONEXISTFSNAME ZFS file system is not in 'zfs list'.
# 2. Invoke 'zfs share <file system>'.
# 3. Verify that share fails
#

verify_runnable "both"

function cleanup
{
	typeset fs
	for fs in $NONEXISTFSNAME $TESTFS ; do
		log_must unshare_fs $TESTPOOL/$fs
	done
}

typeset -i ret=0

log_assert "Verify that "zfs share" with a non-existent file system fails."

log_onexit cleanup

log_mustnot zfs list $TESTPOOL/$NONEXISTFSNAME

zfs share $TESTPOOL/$NONEXISTFSNAME
ret=$?
(( ret == 1)) || \
	log_fail "'zfs share $TESTPOOL/$NONEXISTFSNAME' " \
		"failed with an unexpected return code of $ret."

log_note "Make sure the file system $TESTPOOL/$NONEXISTFSNAME is unshared"
not_shared $TESTPOOL/$NONEXISTFSNAME || \
	log_fail "File system $TESTPOOL/$NONEXISTFSNAME is unexpectedly shared."

log_pass "'zfs share' with a non-existent file system fails."
