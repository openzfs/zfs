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
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2012, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_destroy/zpool_destroy.cfg


#
# DESCRIPTION:
#	'zpool destroy <pool>' can successfully destroy the specified pool.
#
# STRATEGY:
#	1. Create a storage pool
#	2. Destroy the pool
#	3. Verify the is destroyed successfully
#

verify_runnable "global"

function cleanup
{
	poolexists $TESTPOOL2 && destroy_pool $TESTPOOL2
	datasetexists $TESTPOOL1/$TESTVOL && destroy_dataset $TESTPOOL1/$TESTVOL -f

	typeset pool
	for pool in $TESTPOOL1 $TESTPOOL; do
		poolexists $pool && destroy_pool $pool
	done

	[ -n "$recursive" ] && set_tunable64 VOL_RECURSIVE $recursive
}

set -A datasets "$TESTPOOL" "$TESTPOOL2"

log_assert "'zpool destroy <pool>' can destroy a specified pool."

log_onexit cleanup

create_pool $TESTPOOL $DISK0
create_pool $TESTPOOL1 $DISK1
log_must zfs create -s -V $VOLSIZE $TESTPOOL1/$TESTVOL
block_device_wait
if is_freebsd; then
	typeset recursive=$(get_tunable VOL_RECURSIVE)
	log_must set_tunable64 VOL_RECURSIVE 1
fi
create_pool $TESTPOOL2 $ZVOL_DEVDIR/$TESTPOOL1/$TESTVOL

typeset -i i=0
while (( i < ${#datasets[*]} )); do
	log_must poolexists "${datasets[i]}"
	log_must zpool destroy "${datasets[i]}"
	log_mustnot poolexists "${datasets[i]}"
	((i = i + 1))
done

log_pass "'zpool destroy <pool>' executes successfully"
