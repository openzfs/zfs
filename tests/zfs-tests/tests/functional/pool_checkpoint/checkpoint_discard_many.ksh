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
#	Take a checkpoint and discard checkpointed data twice. The
#	idea is to ensure that the background discard zfs thread is
#	always running and works as expected.
#
# STRATEGY:
#	1. Create pool
#	2. Populate it and then take a checkpoint
#	3. Do some changes afterwards, and then discard checkpoint
#	4. Repeat steps 2 and 3
#

verify_runnable "global"

setup_test_pool
log_onexit cleanup_test_pool

populate_test_pool
log_must zpool checkpoint $TESTPOOL
test_change_state_after_checkpoint
log_must zpool checkpoint -d $TESTPOOL
test_wait_discard_finish

log_must mkfile -n 100M $FS2FILE
log_must randwritecomp $FS2FILE 100
log_must zpool checkpoint $TESTPOOL

log_must randwritecomp $FS2FILE 100
log_must zpool checkpoint -d $TESTPOOL
test_wait_discard_finish

log_pass "Background discarding works as expected."
