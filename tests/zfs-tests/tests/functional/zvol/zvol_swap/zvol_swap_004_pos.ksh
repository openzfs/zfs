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
	datasetexists $vol && log_must zfs destroy $vol
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
