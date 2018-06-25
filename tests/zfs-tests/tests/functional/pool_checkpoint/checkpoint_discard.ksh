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
#	Ensure that we can discard the checkpoint from a pool.
#
# STRATEGY:
#	1. Create pool
#	2. Populate it
#	3. Take checkpoint
#	4. Modify data (include at least one destructive change)
#	5. Discard checkpoint
#	6. Export and attempt to rewind. Rewinding should fail
#	7. Import pool normally and verify state
#

verify_runnable "global"

setup_test_pool
log_onexit cleanup_test_pool

populate_test_pool

log_must zpool checkpoint $TESTPOOL

test_change_state_after_checkpoint

log_must zpool checkpoint -d $TESTPOOL

log_must zpool export $TESTPOOL
log_mustnot zpool import --rewind-to-checkpoint $TESTPOOL

log_must zpool import $TESTPOOL
test_verify_post_checkpoint_state

log_pass "Discard checkpoint from pool."
