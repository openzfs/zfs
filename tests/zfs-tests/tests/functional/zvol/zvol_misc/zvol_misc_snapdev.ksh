#!/bin/ksh -p
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
# Copyright 2017, loli10K <ezomori.nozomu@gmail.com>. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zfs_set/zfs_set_common.kshlib
. $STF_SUITE/tests/functional/zvol/zvol_common.shlib
. $STF_SUITE/tests/functional/zvol/zvol_misc/zvol_misc_common.kshlib

#
# DESCRIPTION:
# Verify that ZFS volume property "snapdev" works as intended.
#
# STRATEGY:
# 1. Verify "snapdev" property does not accept invalid values
# 2. Verify "snapdev" adds and removes device nodes when updated
# 3. Verify "snapdev" is inherited correctly
#

verify_runnable "global"

function cleanup
{
	datasetexists $VOLFS && destroy_dataset $VOLFS -r
	datasetexists $ZVOL && destroy_dataset $ZVOL -r
	log_must zfs inherit snapdev $TESTPOOL
	block_device_wait
	is_linux && udev_cleanup
}

log_assert "Verify that ZFS volume property 'snapdev' works as expected."
log_onexit cleanup

VOLFS="$TESTPOOL/volfs"
ZVOL="$TESTPOOL/vol"
SNAP="$ZVOL@snap"
SNAPDEV="${ZVOL_DEVDIR}/$SNAP"
SUBZVOL="$VOLFS/subvol"
SUBSNAP="$SUBZVOL@snap"
SUBSNAPDEV="${ZVOL_DEVDIR}/$SUBSNAP"

log_must zfs create -o mountpoint=none $VOLFS
log_must zfs create -V $VOLSIZE -s $ZVOL
log_must zfs create -V $VOLSIZE -s $SUBZVOL

# 1. Verify "snapdev" property does not accept invalid values
typeset badvals=("off" "on" "1" "nope" "-")
for badval in ${badvals[@]}
do
	log_mustnot zfs set snapdev="$badval" $ZVOL
done

# 2. Verify "snapdev" adds and removes device nodes when updated
# 2.1 First create a snapshot then change snapdev property
log_must zfs snapshot $SNAP
log_must zfs set snapdev=visible $ZVOL
blockdev_exists $SNAPDEV
log_must zfs set snapdev=hidden $ZVOL
blockdev_missing $SNAPDEV
log_must zfs destroy $SNAP
# 2.2 First set snapdev property then create a snapshot
log_must zfs set snapdev=visible $ZVOL
log_must zfs snapshot $SNAP
blockdev_exists $SNAPDEV
log_must zfs destroy $SNAP
blockdev_missing $SNAPDEV
# 2.3 Verify setting to the same value multiple times does not lead to issues
log_must zfs snapshot $SNAP
log_must zfs set snapdev=visible $ZVOL
blockdev_exists $SNAPDEV
log_must zfs set snapdev=visible $ZVOL
blockdev_exists $SNAPDEV
log_must zfs set snapdev=hidden $ZVOL
blockdev_missing $SNAPDEV
log_must zfs set snapdev=hidden $ZVOL
blockdev_missing $SNAPDEV
log_must zfs destroy $SNAP

# 3. Verify "snapdev" is inherited correctly
# 3.1 Check snapdev=visible case
log_must zfs snapshot $SNAP
log_must zfs inherit snapdev $ZVOL
log_must zfs set snapdev=visible $TESTPOOL
verify_inherited 'snapdev' 'visible' $ZVOL $TESTPOOL
blockdev_exists $SNAPDEV
# 3.2 Check snapdev=hidden case
log_must zfs set snapdev=hidden $TESTPOOL
verify_inherited 'snapdev' 'hidden' $ZVOL $TESTPOOL
blockdev_missing $SNAPDEV
# 3.3 Check inheritance on multiple levels
log_must zfs snapshot $SUBSNAP
log_must zfs inherit snapdev $SUBZVOL
log_must zfs set snapdev=hidden $VOLFS
log_must zfs set snapdev=visible $TESTPOOL
verify_inherited 'snapdev' 'hidden' $SUBZVOL $VOLFS
blockdev_missing $SUBSNAPDEV
blockdev_exists $SNAPDEV
log_must zfs destroy $SNAP

# 4. Verify "rename" is correctly reflected when "snapdev=visible"
# 4.1 First create a snapshot and verify the device is present
log_must zfs snapshot $SNAP
log_must zfs set snapdev=visible $ZVOL
blockdev_exists $SNAPDEV
# 4.2 rename the snapshot and verify the devices are updated
log_must zfs rename $SNAP $SNAP-new
blockdev_missing $SNAPDEV
blockdev_exists $SNAPDEV-new
# 4.3 cleanup
log_must zfs destroy $SNAP-new

log_pass "ZFS volume property 'snapdev' works as expected"
