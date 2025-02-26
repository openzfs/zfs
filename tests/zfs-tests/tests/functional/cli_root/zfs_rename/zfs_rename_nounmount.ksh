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
# Copyright (c) 2019 iXsystems, Inc.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	zfs rename -u should rename datasets without unmounting them
#
# STRATEGY:
#	1. Create a set of nested datasets.
#	2. Verify datasets are mounted.
#	3. Rename with -u and verify all datasets stayed mounted.
#

verify_runnable "both"

function rename_cleanup
{
	cd $back
	zfs destroy -fR $TESTPOOL/rename_test
	zfs destroy -fR $TESTPOOL/renamed
}

back=$PWD
log_onexit rename_cleanup

log_must zfs create $TESTPOOL/rename_test
log_must zfs create $TESTPOOL/rename_test/child
log_must zfs create $TESTPOOL/rename_test/child/grandchild

if ! ismounted $TESTPOOL/rename_test; then
	log_fail "$TESTPOOL/rename_test is not mounted"
fi
if ! ismounted $TESTPOOL/rename_test/child; then
	log_fail "$TESTPOOL/rename_test/child is not mounted"
fi
if ! ismounted $TESTPOOL/rename_test/child/grandchild; then
	log_fail "$TESTPOOL/rename_test/child/grandchild is not mounted"
fi

mntp_p=$(get_prop mountpoint $TESTPOOL/rename_test)
mntp_c=$(get_prop mountpoint $TESTPOOL/rename_test/child)
mntp_g=$(get_prop mountpoint $TESTPOOL/rename_test/child/grandchild)

log_must cd $mntp_g
log_mustnot zfs rename $TESTPOOL/rename_test $TESTPOOL/renamed
log_must zfs rename -u $TESTPOOL/rename_test $TESTPOOL/renamed

log_mustnot zfs list $TESTPOOL/rename_test
log_mustnot zfs list $TESTPOOL/rename_test/child
log_mustnot zfs list $TESTPOOL/rename_test/child/grandchild

log_must zfs list $TESTPOOL/renamed
log_must zfs list $TESTPOOL/renamed/child
log_must zfs list $TESTPOOL/renamed/child/grandchild

missing=$(zfs mount | awk \
    -v genpat=$TESTPOOL/renamed \
    -v mntp_p=$mntp_p \
    -v mntp_c=$mntp_c \
    -v mntp_g=$mntp_g '
    BEGIN { p = c = g = 0 }
    $1 ~ genpat && $2 == mntp_p { p = 1 }
    $1 ~ genpat && $2 == mntp_c { c = 1 }
    $1 ~ genpat && $2 == mntp_g { g = 1 }
    END {
	if (p != 1)
		print mntp_p
	if (c != 1)
		print mntp_c
	if (g != 1)
		print mntp_g
    }')
[[ -z "$missing" ]] || log_fail "Mountpoints no longer mounted: $missing"

log_pass "Verified rename -u does not unmount datasets"
