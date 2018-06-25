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
# Copyright (c) 2017, 2018 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/pool_checkpoint/pool_checkpoint.kshlib

#
# DESCRIPTION:
#	Rewind to checkpoint on a stressed pool. We basically try to
#	fragment the pool before and after taking a checkpoint and
#	see if zdb finds any checksum or other errors that imply that
#	blocks from the checkpoint have been reused.
#
# STRATEGY:
#	1. Import pool that's slightly fragmented
#	2. Take checkpoint
#	3. Apply a destructive action and do more random writes
#	4. Run zdb on both current and checkpointed data and make
#	   sure that zdb returns with no errors
#	5. Rewind to checkpoint
#	6. Run zdb again
#

verify_runnable "global"

setup_nested_pool_state
log_onexit cleanup_nested_pools

log_must zpool checkpoint $NESTEDPOOL

#
# Destroy one dataset, modify an existing one and create a
# a new one. Do more random writes in an attempt to raise
# more fragmentation. Then verify both current and checkpointed
# states.
#
fragment_after_checkpoint_and_verify

log_must zpool export $NESTEDPOOL
log_must zpool import -d $FILEDISKDIR --rewind-to-checkpoint $NESTEDPOOL

log_must zdb $NESTEDPOOL

log_pass "Rewind to checkpoint on a stressed pool."
