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

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	'zpool destroy' refuses a pool imported read-only and leaves it
#	imported, its file system mounted.
#
# STRATEGY:
#	1. Create a pool with a file system on a file vdev and export it
#	2. Import it read-only; 'zpool destroy' must fail, with or
#	   without -f
#	3. The pool must still be imported and its file system mounted
#	4. Export it; 'zpool import' must list it and 'zpool import -D'
#	   must not
#	5. Import it read-write and destroy it; now 'zpool import -D' must
#	   list it and 'zpool import' must not
#
# A pool imported read-only does not write its labels, so a destroy
# cannot mark them destroyed: the pool would still be listed, and
# imported by name without -f or -D, as an exported one.
#

verify_runnable "global"

DEVICE_DIR=$TEST_BASE_DIR/dev_destroy-readonly-test
MNTPT=$TEST_BASE_DIR/destroy-readonly-mnt

function cleanup
{
	poolexists $TESTPOOL1 && destroy_pool $TESTPOOL1
	log_must rm -rf $DEVICE_DIR $MNTPT
}

# Whether 'zpool import' lists the pool; -D lists destroyed pools.
function listed # [-D]
{
	zpool import "$@" -d $DEVICE_DIR | grep -q "pool: $TESTPOOL1\$"
}

log_assert "'zpool destroy' refuses a pool imported read-only"

log_onexit cleanup

log_must mkdir -p $DEVICE_DIR
log_must truncate -s $MINVDEVSIZE $DEVICE_DIR/disk
log_must zpool create -f -O mountpoint=none $TESTPOOL1 $DEVICE_DIR/disk
log_must zfs create -o mountpoint=$MNTPT $TESTPOOL1/$TESTFS1
log_must zpool export $TESTPOOL1

log_must zpool import -d $DEVICE_DIR -o readonly=on $TESTPOOL1
log_must test "$(get_pool_prop readonly $TESTPOOL1)" = "on"
log_mustnot zpool destroy $TESTPOOL1
log_mustnot zpool destroy -f $TESTPOOL1
log_must poolexists $TESTPOOL1
log_must ismounted $TESTPOOL1/$TESTFS1

log_must zpool export $TESTPOOL1
log_must listed
log_mustnot listed -D

log_must zpool import -d $DEVICE_DIR $TESTPOOL1
log_must zpool destroy $TESTPOOL1
log_mustnot poolexists $TESTPOOL1
log_must listed -D
log_mustnot listed

log_pass "'zpool destroy' refuses a pool imported read-only"
