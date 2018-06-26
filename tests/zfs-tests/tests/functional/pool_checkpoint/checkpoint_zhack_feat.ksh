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
#	Ensure that we can rewind to a checkpointed state that was
#	before a readonly-compatible feature was introduced.
#
# STRATEGY:
#	1. Create pool
#	2. Populate it
#	3. Take checkpoint
#	4. Modify data (include at least one destructive change) 
#	5. Export pool
#	6. Introduce a new feature in the pool which is unsupported
#	   but readonly-compatible and increment its reference
#	   number so it is marked active.
#	7. Verify that the pool can't be opened writeable, but we
#	   can rewind to the checkpoint (before the feature was 
#	   introduced) if we want to.
#

verify_runnable "global"

#
# Clear all labels from all vdevs so zhack
# doesn't get confused
#
for disk in ${DISKS[@]}; do
	zpool labelclear -f $disk
done

setup_test_pool
log_onexit cleanup_test_pool

populate_test_pool
log_must zpool checkpoint $TESTPOOL
test_change_state_after_checkpoint

log_must zpool export $TESTPOOL

log_must zhack feature enable -r $TESTPOOL 'com.company:future_feature'
log_must zhack feature ref $TESTPOOL 'com.company:future_feature'

log_mustnot zpool import $TESTPOOL
log_must zpool import --rewind-to-checkpoint $TESTPOOL

test_verify_pre_checkpoint_state

log_pass "Rewind to checkpoint from unsupported pool feature."
