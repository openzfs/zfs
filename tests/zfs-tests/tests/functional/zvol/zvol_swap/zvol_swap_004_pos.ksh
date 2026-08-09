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
. $STF_SUITE/tests/functional/zvol/zvol_common.shlib

#
# DESCRIPTION:
#	When a swap zvol is added its volsize does not change.
#
# STRATEGY:
#	1. Determine what 1/4 arc_c_max is.
#	2. Create a zvols in a variety of sizes.
#	3. Add them as swap, and verify the volsize is not changed.
#

verify_runnable "global"

function cleanup
{
	is_swap_inuse $swapname && log_must swap_cleanup $swapname
	datasetexists $vol && destroy_dataset $vol
}

log_assert "For an added swap zvol, (2G <= volsize <= 16G)"

log_onexit cleanup

for vbs in 8192 16384 32768 65536 131072; do
	for multiplier in 32 16384 131072; do
		((volsize = vbs * multiplier))
		vol="$TESTPOOL/vol_$volsize"
		swapname="${ZVOL_DEVDIR}/$vol"

		# Create a sparse volume to test larger sizes
		log_must zfs create -s -b $vbs -V $volsize $vol
		block_device_wait $swapname
		log_must swap_setup $swapname

		new_volsize=$(get_prop volsize $vol)
		[[ $volsize -eq $new_volsize ]] || log_fail "$volsize $new_volsize"

		log_must swap_cleanup $swapname
		log_must_busy zfs destroy $vol
	done
done

log_pass "For an added swap zvol, (2G <= volsize <= 16G)"
