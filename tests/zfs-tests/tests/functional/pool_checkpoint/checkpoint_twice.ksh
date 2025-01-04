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
# http://www.illumos.org/license/CDDL.
#

#
# Copyright (c) 2017 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/pool_checkpoint/pool_checkpoint.kshlib

#
# DESCRIPTION:
#	Attempt to take a checkpoint for an already
#	checkpointed pool. The attempt should fail.
#
# STRATEGY:
#	1. Create pool
#	2. Checkpoint it
#	3. Attempt to checkpoint it again (should fail).
#

verify_runnable "global"

setup_test_pool
log_onexit cleanup_test_pool

log_must zpool checkpoint $TESTPOOL
log_mustnot zpool checkpoint $TESTPOOL

log_pass "Attempting to checkpoint an already checkpointed " \
	"pool fails as expected."
