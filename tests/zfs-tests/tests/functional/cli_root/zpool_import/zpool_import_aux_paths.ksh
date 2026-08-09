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
# Copyright (c) 2026, MorganaFuture. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	Spare and l2cache vdev paths read from the MOS must survive an
#	import which searches the default locations, as long as they still
#	refer to the expected device.  Importing from user supplied
#	directories (-d) must keep rewriting them, and paths which no
#	longer refer to the device must be replaced with a scanned name.
#
# STRATEGY:
#	1. Create a pool on scsi_debug partitions whose cache and spare
#	   devices are addressed by custom persistent symlinks, i.e. names
#	   the default device scan will never report.
#	2. Export and import the pool without -d, then verify the cache
#	   and spare paths were kept.
#	3. Import the pool from a directory of differently named symlinks
#	   with -d, then verify the cache and spare paths were rewritten.
#	4. Remove the symlinks the cache and spare paths now refer to,
#	   import without -d, then verify the stale paths were replaced
#	   with reachable ones and the pool is healthy.
#
# NOTE:
#	This locks the aux path handling of the import listing.  A pool
#	created by a current release records the persistent path in the aux
#	label, so step 2 already passes on unpatched code; the fix also
#	keeps paths for pools whose aux label predates that path being
#	recorded, which the standard tools cannot reproduce here, so that
#	case is not exercised directly.  Steps 3 and 4 cover the -d rewrite
#	and stale-path replacement behavior the fix must preserve.
#

verify_runnable "global"

DEVLINK_DIR=$TEST_BASE_DIR/devlink_aux-test
DEVLINK_DIR2=$TEST_BASE_DIR/devlink_aux-test.2
SDSIZE_MB=400

function cleanup
{
	destroy_pool $TESTPOOL1
	rm -rf $DEVLINK_DIR $DEVLINK_DIR2
	if lsmod | grep -q scsi_debug; then
		unload_scsi_debug
	fi
}

log_assert "Import keeps valid aux vdev paths, rewrites stale or -d ones"

log_onexit cleanup

load_scsi_debug $SDSIZE_MB 1 1 1 '512b'
SDDEVICE=$(get_debug_device)
[[ -n $SDDEVICE && $SDDEVICE != "-" ]] || \
    log_unsupported "Could not find scsi_debug device"

log_must parted /dev/$SDDEVICE --script mklabel gpt
log_must parted /dev/$SDDEVICE --script mkpart primary 1MiB 133MiB
log_must parted /dev/$SDDEVICE --script mkpart primary 133MiB 266MiB
log_must parted /dev/$SDDEVICE --script mkpart primary 266MiB 399MiB
block_device_wait /dev/${SDDEVICE}1 /dev/${SDDEVICE}2 /dev/${SDDEVICE}3

log_must mkdir -p $DEVLINK_DIR $DEVLINK_DIR2
log_must ln -s /dev/${SDDEVICE}2 $DEVLINK_DIR/cache-link
log_must ln -s /dev/${SDDEVICE}3 $DEVLINK_DIR/spare-link
log_must ln -s /dev/${SDDEVICE}1 $DEVLINK_DIR2/main2
log_must ln -s /dev/${SDDEVICE}2 $DEVLINK_DIR2/cache2
log_must ln -s /dev/${SDDEVICE}3 $DEVLINK_DIR2/spare2

log_must zpool create -f $TESTPOOL1 /dev/${SDDEVICE}1 \
    cache $DEVLINK_DIR/cache-link spare $DEVLINK_DIR/spare-link

# A default (blkid) import must keep the custom aux names: they still
# refer to the right devices even though no scan reports them.
log_must zpool export $TESTPOOL1
log_must zpool import $TESTPOOL1
log_must eval "zpool status -P $TESTPOOL1 | grep -q $DEVLINK_DIR/cache-link"
log_must eval "zpool status -P $TESTPOOL1 | grep -q $DEVLINK_DIR/spare-link"

# An import from user supplied directories must keep rewriting the aux
# paths to the scanned names, even though the old names are still valid.
log_must zpool export $TESTPOOL1
log_must zpool import -d $DEVLINK_DIR2 $TESTPOOL1
log_must eval "zpool status -P $TESTPOOL1 | grep -q $DEVLINK_DIR2/cache2"
log_must eval "zpool status -P $TESTPOOL1 | grep -q $DEVLINK_DIR2/spare2"

# Aux paths which no longer refer to the device must not be kept by a
# default import; they are rewritten to a name found by the scan.
log_must zpool export $TESTPOOL1
log_must rm $DEVLINK_DIR2/cache2 $DEVLINK_DIR2/spare2
log_must zpool import $TESTPOOL1
log_mustnot eval "zpool status -P $TESTPOOL1 | grep -q $DEVLINK_DIR2/cache2"
log_mustnot eval "zpool status -P $TESTPOOL1 | grep -q $DEVLINK_DIR2/spare2"
log_mustnot eval "zpool status $TESTPOOL1 | grep -q UNAVAIL"

log_pass "Import keeps valid aux vdev paths, rewrites stale or -d ones"
