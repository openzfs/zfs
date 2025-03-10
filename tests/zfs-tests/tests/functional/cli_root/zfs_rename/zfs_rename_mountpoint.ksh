#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# CDDL HEADER START
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy is of the CDDL is also available via the Internet
# at http://www.illumos.org/license/CDDL.
#
# CDDL HEADER END
#

#
# Copyright (c) 2018 Datto Inc.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	zfs rename should rename datasets even for mountpoint=none children
#
# STRATEGY:
#	1. Create a set of nested datasets with mountpoint=none
#	2. Verify datasets aren't mounted except for the parent
#	3. Rename mountpoint and verify all child datasets are renamed
#

verify_runnable "both"

function rename_cleanup
{
	zfs destroy -fR $TESTPOOL/rename_test
	zfs destroy -fR $TESTPOOL/renamed
}

log_onexit rename_cleanup


log_must zfs create $TESTPOOL/rename_test
log_must zfs create $TESTPOOL/rename_test/ds
log_must zfs create -o mountpoint=none $TESTPOOL/rename_test/child
log_must zfs create $TESTPOOL/rename_test/child/grandchild

if ! ismounted $TESTPOOL/rename_test; then
	log_fail "$TESTPOOL/rename_test is not mounted"
fi
if ! ismounted $TESTPOOL/rename_test/ds; then
	log_fail "$TESTPOOL/rename_test/ds is not mounted"
fi
if ismounted $TESTPOOL/rename_test/child; then
	log_fail "$TESTPOOL/rename_test/child is mounted"
fi
if ismounted $TESTPOOL/rename_test/child/grandchild; then
	log_fail "$TESTPOOL/rename_test/child/grandchild is mounted"
fi

log_must zfs rename $TESTPOOL/rename_test $TESTPOOL/renamed

log_mustnot zfs list $TESTPOOL/rename_test
log_mustnot zfs list $TESTPOOL/rename_test/ds
log_mustnot zfs list $TESTPOOL/rename_test/child
log_mustnot zfs list $TESTPOOL/rename_test/child/grandchild

log_must zfs list $TESTPOOL/renamed
log_must zfs list $TESTPOOL/renamed/ds
log_must zfs list $TESTPOOL/renamed/child
log_must zfs list $TESTPOOL/renamed/child/grandchild

if ! ismounted $TESTPOOL/renamed; then
        log_must zfs get all $TESTPOOL/renamed
	log_fail "$TESTPOOL/renamed is not mounted"
fi
if ! ismounted $TESTPOOL/renamed/ds; then
	log_fail "$TESTPOOL/renamed/ds is not mounted"
fi
if ismounted $TESTPOOL/renamed/child; then
	log_fail "$TESTPOOL/renamed/child is mounted"
fi
if ismounted $TESTPOOL/renamed/child/grandchild; then
	log_fail "$TESTPOOL/renamed/child/grandchild is mounted"
fi

log_pass "Verified rename for mountpoint=none children."
