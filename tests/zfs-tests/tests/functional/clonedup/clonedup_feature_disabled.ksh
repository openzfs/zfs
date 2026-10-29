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
# Copyright (c) 2026, Patrick Domack. All rights reserved.
#

. $STF_SUITE/tests/functional/clonedup/clonedup.kshlib

#
# DESCRIPTION:
#	Without the clonedup feature a run cannot start and nothing is
#	written to the pool.  A dry run writes nothing either way, so
#	it runs without the feature.  Enabling it later lets a real
#	run start.  Without block_cloning both are refused, and
#	enabling clonedup does not enable block_cloning.  Each
#	refusal names the missing feature.
#

verify_runnable "global"

log_assert "zpool clonedup needs feature@clonedup"
log_onexit clonedup_cleanup

log_must zpool create -f -o feature@clonedup=disabled \
    -O compression=off $TESTPOOL $(clonedup_disks)
clonedup_write /$TESTPOOL/a
clonedup_dup /$TESTPOOL/a /$TESTPOOL/b
clonedup_sync

log_mustnot_expect "the clonedup feature is not enabled" \
    zpool clonedup $TESTPOOL
log_must zpool clonedup -n -w $TESTPOOL
log_must [ "$(clonedup_jstat clonedup_candidates)" -eq $CD_BLOCKS ]
clonedup_check_shared $TESTPOOL a $TESTPOOL b ""
log_must [ "$(get_pool_prop feature@clonedup $TESTPOOL)" \
    = "disabled" ]
typeset out
out=$(zdb -dddd $TESTPOOL 1) ||
    log_fail "zdb -dddd $TESTPOOL 1 failed"
log_mustnot eval "print -r -- \"\$out\" | grep -q clonedup_scan"
log_mustnot eval \
    "print -r -- \"\$out\" | grep -qE '^[[:space:]]+scan = '"

log_must zpool set feature@clonedup=enabled $TESTPOOL
clonedup_run
clonedup_check_shared $TESTPOOL a $TESTPOOL b "$(clonedup_all_blocks)"
log_must [ "$(get_pool_prop feature@clonedup $TESTPOOL)" = "enabled" ]

# clonedup does not drag block_cloning in; without it a run is refused
log_must zpool destroy -f $TESTPOOL
log_must zpool create -f -o feature@block_cloning=disabled \
    -O compression=off $TESTPOOL $(clonedup_disks)
log_must [ "$(get_pool_prop feature@block_cloning $TESTPOOL)" \
    = "disabled" ]
log_must [ "$(get_pool_prop feature@clonedup $TESTPOOL)" = "enabled" ]
clonedup_write /$TESTPOOL/a
clonedup_dup /$TESTPOOL/a /$TESTPOOL/b
clonedup_sync
log_mustnot_expect "the block_cloning feature is not enabled" \
    zpool clonedup $TESTPOOL
log_mustnot_expect "the block_cloning feature is not enabled" \
    zpool clonedup -n $TESTPOOL
log_must [ "$(get_pool_prop feature@block_cloning $TESTPOOL)" \
    = "disabled" ]
log_must zpool set feature@block_cloning=enabled $TESTPOOL
clonedup_run
clonedup_check_shared $TESTPOOL a $TESTPOOL b "$(clonedup_all_blocks)"

log_pass "zpool clonedup needs feature@clonedup"
