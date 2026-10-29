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
#	clonedup=source lets a dataset's blocks be shared by others
#	but never rewrites them; clonedup=off excludes a dataset
#	entirely, is inherited, and can be set back to on under an
#	excluded parent.
#

verify_runnable "global"

log_assert "the clonedup dataset property is honored"
log_onexit clonedup_cleanup

clonedup_pool_create
log_must zfs create $TESTPOOL/src
log_must [ "$(get_prop clonedup $TESTPOOL/src)" = "on" ]
log_must zfs set clonedup=source $TESTPOOL/src
log_must [ "$(get_prop clonedup $TESTPOOL/src)" = "source" ]

clonedup_write /$TESTPOOL/src/s
clonedup_dup /$TESTPOOL/src/s /$TESTPOOL/x
clonedup_write /$TESTPOOL/a
clonedup_dup /$TESTPOOL/a /$TESTPOOL/src/t
clonedup_sync
log_must zfs snapshot $TESTPOOL/src@before
clonedup_run

# x shares s and a shares t: the source dataset's blocks survive and
# are never rewritten, so t still holds the blocks it had before.
clonedup_check_shared $TESTPOOL/src s $TESTPOOL x \
    "$(clonedup_all_blocks)"
clonedup_check_shared $TESTPOOL a $TESTPOOL/src t \
    "$(clonedup_all_blocks)"
clonedup_check_shared $TESTPOOL/src@before t $TESTPOOL/src t \
    "$(clonedup_all_blocks)"
clonedup_check_shared $TESTPOOL/src@before s $TESTPOOL/src s \
    "$(clonedup_all_blocks)"

log_must zfs set clonedup=off $TESTPOOL/src
log_must zfs create $TESTPOOL/src/child
log_must [ "$(get_prop clonedup $TESTPOOL/src/child)" = "off" ]
clonedup_dup /$TESTPOOL/src/s /$TESTPOOL/src/u
clonedup_dup /$TESTPOOL/src/s /$TESTPOOL/src/child/v
clonedup_sync
clonedup_run
clonedup_check_shared $TESTPOOL/src s $TESTPOOL/src u ""
clonedup_check_shared $TESTPOOL/src s $TESTPOOL/src/child v ""
clonedup_check_shared $TESTPOOL/src u $TESTPOOL/src/child v ""
clonedup_jstat_is clonedup_indexed 0

# An excluded parent must not exclude a child that sets the property
# back.  A run skips the block tree of an excluded dataset, so the
# child has to be reached as a dataset of its own and not through the
# parent it hangs under.  The last check is the converse: the parent
# is still excluded after a run that cloned inside its child.
log_must zfs create $TESTPOOL/src/kept
log_must zfs set clonedup=on $TESTPOOL/src/kept
log_must [ "$(get_prop clonedup $TESTPOOL/src/kept)" = "on" ]
clonedup_write /$TESTPOOL/src/kept/p
clonedup_dup /$TESTPOOL/src/kept/p /$TESTPOOL/src/kept/q
clonedup_sync
clonedup_run
clonedup_check_shared $TESTPOOL/src/kept p $TESTPOOL/src/kept q \
    "$(clonedup_all_blocks)"
clonedup_check_shared $TESTPOOL/src s $TESTPOOL/src u ""
clonedup_check_shared $TESTPOOL/src u $TESTPOOL/src/child v ""

log_must zfs inherit clonedup $TESTPOOL/src
log_must [ "$(get_prop clonedup $TESTPOOL/src)" = "on" ]
log_mustnot zfs set clonedup=maybe $TESTPOOL/src
clonedup_leakcheck

log_pass "the clonedup dataset property is honored"
