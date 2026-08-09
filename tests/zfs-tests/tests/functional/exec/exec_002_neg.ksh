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
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# When set property exec=off on a filesystem, processes can not be executed from
# this filesystem.
#
# STRATEGY:
# 1. Create pool and file system.
# 2. Copy '/usr/bin/ls' to the ZFS file system.
# 3. Setting exec=off on this file system.
# 4. Make sure '/usr/bin/ls' can not work in this ZFS file system.
# 5. Make sure mmap which is using the PROT_EXEC calls failed.
#

verify_runnable "both"

function cleanup
{
	log_must rm $TESTDIR/myls
}

#
# Execute and check if the return value is equal to expected.
#
# $1 expected value
# $2..$n executed item
#
function exec_n_check
{
	typeset expect_value=$1
	shift
	"$@"
	log_must [ $? = $expect_value ]
}

log_assert "Setting exec=off on a filesystem, processes can not be executed" \
	"from this file system."
log_onexit cleanup

log_must cp  $STF_PATH/ls $TESTDIR/myls
log_must zfs set exec=off $TESTPOOL/$TESTFS

if is_linux; then
	exp=1 # EPERM
else
	exp=13 # EACCES
fi
log_must exec_n_check 126  $TESTDIR/myls
log_must exec_n_check $exp mmap_exec $TESTDIR/myls

log_pass "Setting exec=off on filesystem testing passed."
