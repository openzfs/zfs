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

. $STF_SUITE/tests/functional/cli_root/zpool_import/zpool_import.kshlib

#
# DESCRIPTION:
#	A hot spare that was missing when the pool was imported is still a
#	hot spare once it is back: replacing a device with it swaps it in
#	as a spare, and the replaced device stays in the pool.
#
# STRATEGY:
#	1. Create a mirror pool with a hot spare, and export it.
#	2. Move the spare out of the device directory, and import the pool
#		from that directory: the spare is UNAVAIL.
#	3. Put the spare back, replace a side of the mirror with it, and
#		wait for the resilver.
#	4. Verify that the spare is under a spare vdev, that the replaced
#		device is still in the pool, and that the spare is INUSE.
#

verify_runnable "global"

function custom_cleanup
{
	log_must rm -rf $BACKUP_DEVICE_DIR
	cleanup
}

log_onexit custom_cleanup

function check_spare_state # state
{
	typeset state=$(get_device_state $TESTPOOL1 $VDEV2 spares)

	if [[ "$state" != "$1" ]]; then
		zpool status -v $TESTPOOL1
		log_fail "The spare is '$state', expected '$1'."
	fi
}

log_assert "A hot spare missing at import is still a hot spare once back."

log_must zpool create $TESTPOOL1 mirror $VDEV0 $VDEV1 spare $VDEV2
log_must_busy zpool export $TESTPOOL1

log_must mkdir -p $BACKUP_DEVICE_DIR
log_must mv $VDEV2 $BACKUP_DEVICE_DIR
log_must zpool import -d $DEVICE_DIR $TESTPOOL1
check_spare_state UNAVAIL

log_must mv $BACKUP_DEVICE_DIR/$(basename $VDEV2) $DEVICE_DIR
log_must zpool replace $TESTPOOL1 $VDEV0 $VDEV2
log_must zpool wait -t resilver $TESTPOOL1
if ! zpool status $TESTPOOL1 | grep -q 'spare-0'; then
	zpool status -v $TESTPOOL1
	log_fail "The spare did not go in as a hot spare."
fi
log_must check_state $TESTPOOL1 $VDEV0 ONLINE
check_spare_state INUSE

log_must zpool destroy $TESTPOOL1

log_pass "A hot spare missing at import is still a hot spare once back."
