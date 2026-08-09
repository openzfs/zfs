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
# Copyright 2017, loli10K <ezomori.nozomu@gmail.com>. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/zvol/zvol_common.shlib
. $STF_SUITE/tests/functional/zvol/zvol_misc/zvol_misc_common.kshlib

#
# DESCRIPTION:
# Verify ZIL functionality on ZVOLs
#
# STRATEGY:
# 1. Create a ZVOLs with various combination of "logbias" and "sync" values
# 2. Write data to ZVOL device node
# 3. Verify we don't trigger any issue like the one reported in #6238
#

verify_runnable "global"

function cleanup
{
	datasetexists $ZVOL && destroy_dataset $ZVOL
	block_device_wait
}

log_assert "Verify ZIL functionality on ZVOLs"
log_onexit cleanup

ZVOL="$TESTPOOL/vol"
ZDEV="$ZVOL_DEVDIR/$ZVOL"
typeset -a logbias_prop_vals=('latency' 'throughput')
typeset -a sync_prop_vals=('standard' 'always' 'disabled')

for logbias in ${logbias_prop_vals[@]}; do
	for sync in ${sync_prop_vals[@]}; do
		# 1. Create a ZVOL with logbias=throughput and sync=always
		log_must zfs create -V $VOLSIZE -b 128K -o sync=$sync \
		    -o logbias=$logbias $ZVOL

		# 2. Write data to its device node
		for i in {1..50}; do
			dd if=/dev/zero of=$ZDEV bs=8k count=1 &
		done

		# 3. Verify we don't trigger any issue
		log_must wait
		log_must_busy zfs destroy $ZVOL
	done
done

log_pass "ZIL functionality works on ZVOLs"
