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
# Copyright (c) 2017, 2018 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/pool_checkpoint/pool_checkpoint.kshlib

#
# DESCRIPTION:
#	Ensure that checkpoint plays well with indirect mappings
#	and blocks.
#
# STRATEGY:
#	1. Import pool that's slightly fragmented
#	2. Introduce indirection by removing and re-adding devices
#	3. Take checkpoint
#	4. Apply a destructive action and do more random writes
#	5. Run zdb on both current and checkpointed data and make
#	   sure that zdb returns with no errors
#

verify_runnable "global"

setup_nested_pool_state
log_onexit cleanup_nested_pools

#
# Remove and re-add all disks.
#
introduce_indirection

#
# Display fragmentation after removals
#
log_must zpool list -v

log_must zpool checkpoint $NESTEDPOOL

#
# Destroy one dataset, modify an existing one and create a
# a new one. Do more random writes in an attempt to raise
# more fragmentation. Then verify both current and checkpointed
# states.
#
fragment_after_checkpoint_and_verify

log_pass "Running correctly on indirect setups with a checkpoint."
