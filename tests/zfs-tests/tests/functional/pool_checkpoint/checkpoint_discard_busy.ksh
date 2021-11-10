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
#	Discard checkpoint on a stressed pool. Ensure that we can
#	export and import the pool while discarding but not run any
#	operations that have to do with the checkpoint or change the
#	pool's config.
#
# STRATEGY:
#	1. Import pools that's slightly fragmented
#	2. Take checkpoint
#	3. Do more random writes to "free" checkpointed blocks
#	4. Start discarding checkpoint
#	5. Export pool while discarding checkpoint
#	6. Attempt to rewind (should fail)
#	7. Import pool and ensure that discard is still running
#	8. Attempt to run checkpoint commands, or commands that
#	   change the pool's config (should fail)
#

verify_runnable "global"

function test_cleanup
{
	# reset memory limit to 16M
	set_tunable64 SPA_DISCARD_MEMORY_LIMIT 1000000
	cleanup_nested_pools
}

setup_nested_pool_state
log_onexit test_cleanup

#
# Force discard to happen slower so it spans over
# multiple txgs.
#
# Set memory limit to 128 bytes. Assuming that we
# use 64-bit words for encoding space map entries,
# ZFS will discard 8 non-debug entries per txg
# (so at most 16 space map entries in debug-builds
# due to debug entries).
#
# That should give us more than enough txgs to be
# discarding the checkpoint for a long time as with
# the current setup the checkpoint space maps should
# have tens of thousands of entries.
#
# Note: If two-words entries are used in the space
#	map, we should have even more time to
#	verify this.
#
set_tunable64 SPA_DISCARD_MEMORY_LIMIT 128

log_must zpool checkpoint $NESTEDPOOL

fragment_after_checkpoint_and_verify

log_must zpool checkpoint -d $NESTEDPOOL

log_must zpool export $NESTEDPOOL

#
# Verify on-disk state while pool is exported
#
log_must zdb -e -p $FILEDISKDIR $NESTEDPOOL

#
# Attempt to rewind on a pool that is discarding
# a checkpoint.
#
log_mustnot zpool import -d $FILEDISKDIR --rewind-to-checkpoint $NESTEDPOOL

log_must zpool import -d $FILEDISKDIR $NESTEDPOOL

#
# Discarding should continue after import, so
# all the following operations should fail.
#
log_mustnot zpool checkpoint $NESTEDPOOL
log_mustnot zpool checkpoint -d $NESTEDPOOL
log_mustnot zpool remove $NESTEDPOOL $FILEDISK1
log_mustnot zpool reguid $NESTEDPOOL

# reset memory limit to 16M
set_tunable64 SPA_DISCARD_MEMORY_LIMIT 16777216

nested_wait_discard_finish

log_must zpool export $NESTEDPOOL
log_must zdb -e -p $FILEDISKDIR $NESTEDPOOL

log_pass "Can export/import but not rewind/checkpoint/discard or " \
    "change pool's config while discarding."
