#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# CDDL HEADER START
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# http://www.illumos.org/license/CDDL.
#
# CDDL HEADER END
#

#
# Copyright (c) 2020, George Amanakis. All rights reserved.
# Copyright (c) 2020 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/removal/removal.kshlib

#
# DESCRIPTION:
#	'zpool split' should succeed on pools with indirect vdevs.
#
# STRATEGY:
#	Create a mirrored pool, add a single device, remove it. `zpool split`
#	should succeed.
#

verify_runnable "global"

log_assert "'zpool split' works on pools with indirect VDEVs."

function cleanup
{
	if poolexists $TESTPOOL ; then
		destroy_pool $TESTPOOL
	fi
	if poolexists $TESTPOOL2 ; then
		destroy_pool $TESTPOOL2
	fi
	rm -fd $VDEV_TEMP $VDEV_M1 $VDEV_M2 $altroot
}
log_onexit cleanup

typeset vdev_m12_mb=400
typeset vdev_temp_mb=$(( floor($vdev_m12_mb / 2) ))
typeset VDEV_TEMP="$TEST_BASE_DIR/vdev_temp"
typeset VDEV_M1="$TEST_BASE_DIR/vdev_m1"
typeset VDEV_M2="$TEST_BASE_DIR/vdev_m2"
typeset altroot="$TESTDIR/altroot-$TESTPOOL2"

log_must truncate -s ${vdev_temp_mb}M $VDEV_TEMP
log_must truncate -s ${vdev_m12_mb}M $VDEV_M1
log_must truncate -s ${vdev_m12_mb}M $VDEV_M2

log_must zpool create -f $TESTPOOL $VDEV_TEMP
log_must zpool add -f $TESTPOOL mirror $VDEV_M1 $VDEV_M2
log_must zpool remove $TESTPOOL $VDEV_TEMP
log_must wait_for_removal $TESTPOOL
log_must zpool split -R $altroot $TESTPOOL $TESTPOOL2
log_must poolexists $TESTPOOL2
log_must test "$(get_pool_prop 'altroot' $TESTPOOL2)" = "$altroot"

log_pass "'zpool split' works on pools with indirect VDEVs."
