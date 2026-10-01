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
#	Verify that zpool export and zpool destroy succeed while other
#	processes poll the pool stats.
#
# STRATEGY:
#	1. Create a pool with many vdevs, so building its config is slow
#	2. Run zpool iostat -v in several loops in the background
#	3. Export and import the pool several times
#	4. Destroy the pool while the loops still run
#

verify_runnable "global"

DEVICE_DIR=$TEST_BASE_DIR/dev_export-stats-test
STOP_FILE=$TEST_BASE_DIR/export-stats-stop

function cleanup
{
	touch $STOP_FILE
	wait
	poolexists $TESTPOOL1 && destroy_pool $TESTPOOL1
	log_must rm -rf $DEVICE_DIR $STOP_FILE
}

log_assert "zpool export and destroy succeed while pool stats are polled"

log_onexit cleanup

log_must mkdir -p $DEVICE_DIR
log_must truncate -s $MINVDEVSIZE $DEVICE_DIR/disk{1..200}
log_must zpool create -f -O mountpoint=none $TESTPOOL1 $DEVICE_DIR/disk*

log_must rm -f $STOP_FILE
for i in {1..4}; do
	while [[ ! -e $STOP_FILE ]]; do
		zpool iostat -v $TESTPOOL1 >/dev/null 2>&1
	done &
done

for i in {1..10}; do
	log_must zpool export $TESTPOOL1
	log_must zpool import -d $DEVICE_DIR $TESTPOOL1
done
log_must zpool destroy $TESTPOOL1

log_pass "zpool export and destroy succeed while pool stats are polled"
