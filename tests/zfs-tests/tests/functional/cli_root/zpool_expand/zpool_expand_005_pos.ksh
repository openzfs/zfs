#! /bin/ksh -p
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
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2012, 2018 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/include/blkdev.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_expand/zpool_expand.cfg

#
# DESCRIPTION:
#
# STRATEGY:
# 1) Create a scsi_debug device and a pool based on it
# 2) Expand the device and rescan the scsi bus
# 3) Reopen the pool and check that it detects new available space
# 4) Online the device and check that the pool has been expanded
#

verify_runnable "global"

function cleanup
{
	poolexists $TESTPOOL1 && destroy_pool $TESTPOOL1
	unload_scsi_debug
}

log_onexit cleanup

log_assert "zpool based on scsi device can be expanded with zpool online -e"

# run scsi_debug to create a device
MINVDEVSIZE_MB=$((MINVDEVSIZE / 1048576))
load_scsi_debug $MINVDEVSIZE_MB 1 1 1 '512b'
block_device_wait
SDISK=$(get_debug_device)
log_must zpool create $TESTPOOL1 $SDISK

typeset autoexp=$(get_pool_prop autoexpand $TESTPOOL1)
if [[ $autoexp != "off" ]]; then
	log_fail "zpool $TESTPOOL1 autoexpand should be off but is $autoexp"
fi

typeset prev_size=$(get_pool_prop size $TESTPOOL1)
log_note "original pool size: $prev_size"

# resize the scsi_debug device
echo "5" > /sys/bus/pseudo/drivers/scsi_debug/virtual_gb
# rescan the device to detect the new size
echo "1" > /sys/class/block/$SDISK/device/rescan
block_device_wait

# reopen the pool so ZFS can see the new space
log_must zpool reopen $TESTPOOL1

typeset expandsize=$(get_pool_prop expandsize $TESTPOOL1)
log_note "pool expandsize: $expandsize"
if [[ "$zpool_expandsize" = "-" ]]; then
	log_fail "pool $TESTPOOL1 did not detect any " \
	    "expandsize after reopen"
fi

# online the device so the zpool will use the new space
log_must zpool online -e $TESTPOOL1 $SDISK
log_must zpool sync $TESTPOOL1

typeset new_size=$(get_pool_prop size $TESTPOOL1)
log_note "new pool size: $new_size"
if [[ $new_size -le $prev_size ]]; then
	log_fail "pool $TESTPOOL1 did not expand " \
	    "after vdev expansion and zpool online -e"
fi

log_pass "zpool based on scsi_debug can be expanded with reopen and online -e"
