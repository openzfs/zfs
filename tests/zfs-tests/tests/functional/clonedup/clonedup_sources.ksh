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
#	Blocks in a snapshot whose head copy is gone, and blocks in an
#	unmounted filesystem, still serve as the surviving copy.
#

verify_runnable "global"

function cleanup
{
	rm -f $TEST_BASE_DIR/clonedup_a
	clonedup_cleanup
}

log_assert "snapshot and unmounted blocks are valid sources"
log_onexit cleanup

clonedup_pool_create
typeset all="$(clonedup_all_blocks)"

clonedup_write /$TESTPOOL/a
log_must cp /$TESTPOOL/a $TEST_BASE_DIR/clonedup_a
clonedup_sync
log_must zfs snapshot $TESTPOOL@snap
log_must rm /$TESTPOOL/a
clonedup_dup $TEST_BASE_DIR/clonedup_a /$TESTPOOL/b

log_must zfs create $TESTPOOL/um
clonedup_write /$TESTPOOL/um/p
clonedup_sync
clonedup_dup /$TESTPOOL/um/p /$TESTPOOL/x
clonedup_sync
log_must zfs unmount $TESTPOOL/um

clonedup_run
clonedup_check_shared $TESTPOOL@snap a $TESTPOOL b "$all"
clonedup_check_shared $TESTPOOL/um p $TESTPOOL x "$all"
log_must zfs mount $TESTPOOL/um
log_must cmp /$TESTPOOL/um/p /$TESTPOOL/x
log_must rm -f $TEST_BASE_DIR/clonedup_a
clonedup_leakcheck

log_pass "snapshot and unmounted blocks are valid sources"
