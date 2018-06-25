#!/bin/ksh -p

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

#
# Copyright (c) 2017 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/pool_checkpoint/pool_checkpoint.kshlib

#
# DESCRIPTION:
#	Ensure that we can add a device while the pool has a
#	checkpoint but in the case of a rewind that device does
#	not show up.
#
# STRATEGY:
#	1. Create pool
#	2. Populate it
#	3. Take checkpoint
#	4. Add device and modify data
#	   (include at least one destructive change) 
#	5. Rewind to checkpoint
#	6. Verify that we rewinded successfully and check if the
#	   device shows up in the vdev list
#

verify_runnable "global"

setup_test_pool
log_onexit cleanup_test_pool

populate_test_pool

log_must zpool checkpoint $TESTPOOL
log_must zpool add $TESTPOOL $EXTRATESTDISK

#
# Ensure that the vdev shows up
#
log_must eval "zpool list -v $TESTPOOL | grep $EXTRATESTDISK"
test_change_state_after_checkpoint

log_must zpool export $TESTPOOL
log_must zpool import --rewind-to-checkpoint $TESTPOOL

test_verify_pre_checkpoint_state

#
# Ensure that the vdev doesn't show up after the rewind
#
log_mustnot eval "zpool list -v $TESTPOOL | grep $EXTRATESTDISK"

log_pass "Add device in checkpointed pool."
