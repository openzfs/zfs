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
#	-s and -p act only on a clonedup run: they fail when nothing
#	runs and never touch a scrub.
#

verify_runnable "global"

log_assert "zpool clonedup -s and -p never act on a scrub"
log_onexit clonedup_cleanup

clonedup_pool_create
clonedup_write /$TESTPOOL/a
clonedup_sync

log_mustnot zpool clonedup -s $TESTPOOL
log_mustnot zpool clonedup -p $TESTPOOL

clonedup_hold
log_must zpool scrub $TESTPOOL
log_must is_pool_scrubbing $TESTPOOL
log_mustnot zpool clonedup -s $TESTPOOL
log_mustnot zpool clonedup -p $TESTPOOL
log_must is_pool_scrubbing $TESTPOOL
log_mustnot is_pool_scrub_paused $TESTPOOL
log_must zpool scrub -s $TESTPOOL
clonedup_release

log_pass "zpool clonedup -s and -p never act on a scrub"
