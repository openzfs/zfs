#! /bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
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
# Copyright (c) 2021 by Nutanix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Check if longnames are handled correctly by ZIL replay and feature is activated.
#
# STRATEGY:
# 1. Create a zpool with longname feature disabled
# 2. Enable the feature@longname
# 3. Enable 'longname' property on the dataset.
# 4. Freeze the zpool
# 5. Create a longname
# 6. Export and import the zpool.
# 7. Replaying of longname create should activate the feature@longname
verify_runnable "global"

function cleanup
{
	log_must rm -rf $WORKDIR
	poolexists $TESTPOOL && zpool destroy $TESTPOOL
}

log_assert "Check feature@longname and 'longname' dataset propery work correctly"

log_onexit cleanup

poolexists $TESTPOOL && zpool destroy $TESTPOOL

log_must zpool create -o feature@longname=disabled $TESTPOOL $DISKS

log_must zfs create $TESTPOOL/$TESTFS

log_must zfs set mountpoint=$TESTDIR $TESTPOOL/$TESTFS

LONGNAME=$(printf 'a%.0s' {1..512})
LONGFNAME="file-$LONGNAME"
LONGDNAME="dir-$LONGNAME"
SHORTDNAME="dir-short"
SHORTFNAME="file-short"
WORKDIR=$TESTDIR/workdir

log_must mkdir $WORKDIR

log_must zpool set feature@longname=enabled $TESTPOOL
log_must zfs set longname=on $TESTPOOL/$TESTFS

# Ensure that the feature is NOT activated yet as no longnamed file is created.
state=$(zpool get feature@longname -H -o value $TESTPOOL)
log_note "feature@longname on pool: $TESTPOOL : $state"

if [[ "$state" != "enabled" ]]; then
	log_fail "feature@longname has state $state (expected enabled)"
fi

#
# This dd command works around an issue where ZIL records aren't created
# after freezing the pool unless a ZIL header already exists. Create a file
# synchronously to force ZFS to write one out.
#
log_must dd if=/dev/zero of=/$WORKDIR/sync conv=fdatasync,fsync bs=1 count=1

log_must zpool freeze $TESTPOOL

log_must mkdir $WORKDIR/$LONGDNAME
log_must touch $WORKDIR/$LONGFNAME

# Export and re-import the zpool
log_must zpool export $TESTPOOL
log_must zpool import $TESTPOOL

# Ensure that the feature is activated once longnamed files are created.
state=$(zpool get feature@longname -H -o value $TESTPOOL)
log_note "feature@longname on pool: $TESTPOOL : $state"
if [[ "$state" != "active" ]]; then
	log_fail "feature@longname has state $state (expected active)"
fi

# Destroying the dataset where the feature is activated should put the feature
# back to 'enabled' state
log_must zfs destroy -r $TESTPOOL/$TESTFS
state=$(zpool get feature@longname -H -o value $TESTPOOL)
log_note "feature@longname on pool: $TESTPOOL : $state"
if [[ "$state" != "enabled" ]]; then
	log_fail "feature@longname has state $state (expected active)"
fi

log_pass
