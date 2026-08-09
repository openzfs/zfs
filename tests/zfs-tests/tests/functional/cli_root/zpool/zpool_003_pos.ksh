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

#
# DESCRIPTION:
#	Verify debugging features of zpool such as ABORT and freeze/unfreeze
#	should run successfully.
#
# STRATEGY:
# 1. Create an array containing each zpool options.
# 2. For each element, execute the zpool command.
# 3. Verify it run successfully.
#

function cleanup
{
	unset ZFS_ABORT

	log_must pop_coredump_pattern "$coresavepath"
	log_must rm -rf $corepath

	# Don't leave the pool frozen.
	log_must destroy_pool $TESTPOOL
	log_must default_mirror_setup $DISKS
}

verify_runnable "both"

log_assert "Debugging features of zpool should succeed."
log_onexit cleanup

corepath=$TESTDIR/core
corefile=$corepath/core.zpool
coresavepath=$corepath/save
log_must rm -rf $corepath
log_must mkdir $corepath

log_must eval "zpool -? >/dev/null 2>&1"

if is_global_zone; then
	log_must zpool freeze $TESTPOOL
else
	log_mustnot zpool freeze $TESTPOOL
	log_mustnot zpool freeze ${TESTPOOL%%/*}
fi

log_mustnot zpool freeze fakepool

log_must eval "push_coredump_pattern \"$corepath\" > \"$coresavepath\""
log_must export ZFS_ABORT=yes

log_mustnot eval "zpool >/dev/null 2>&1"
log_must [ -f "$corefile" ]

log_pass "Debugging features of zpool succeed."
