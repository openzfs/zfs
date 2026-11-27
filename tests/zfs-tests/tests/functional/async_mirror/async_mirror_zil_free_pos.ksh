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

#
# Copyright (c) 2026, John White.
#

. $STF_SUITE/tests/functional/async_mirror/async_mirror.kshlib

#
# DESCRIPTION:
#	Freed intent log blocks are not held.  The replay never copies
#	them, so their space can be reused at once.
#
# STRATEGY:
#	1. Create a pool with a two-way mirror and turn the async mirror on.
#	2. Set sync=always and slow the lagging child, so that the replay
#	   stays behind.
#	3. Write 8 MiB in records of 4 KiB, which go to the intent log.
#	4. Verify that the held space is below 4 MiB.
#

verify_runnable "global"

log_onexit cleanup_async_mirror

async_mirror_create_pool
async_mirror_enable

typeset guid=$(get_leaf_guid $ASYNC_MIRROR_LAGGING)
log_must zfs set sync=always $TESTPOOL
log_must zinject -d $guid -D 500:1 $TESTPOOL
log_must file_write -o create -f /$TESTPOOL/file -b 4096 -c 2048 -d R
sync_pool $TESTPOOL
typeset -i held=$(get_async_mirror_stat held_bytes)
log_must zinject -c all

log_note "held $held bytes"
log_must test $held -lt 4194304

log_pass "Freed intent log blocks are not held."
