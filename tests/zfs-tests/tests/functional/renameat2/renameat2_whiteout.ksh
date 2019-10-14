#!/bin/ksh -p
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
#

#
# Copyright (C) 2019 Aleksa Sarai <cyphar@cyphar.com>
# Copyright (C) 2019 SUSE LLC
#

. $STF_SUITE/include/libtest.shlib

verify_runnable "both"

function cleanup
{
	log_must rm -rf $TESTDIR/*
}

log_assert "ZFS supports RENAME_WHITEOUT."
log_onexit cleanup

check_feature_flag "feature@rename_whiteout" "$TESTPOOL1" "enabled"

cd $TESTDIR
echo "whiteout" > whiteout

# Straight-forward rename-with-whiteout.
log_must renameat2 -w whiteout new
check_feature_flag "feature@rename_whiteout" "$TESTPOOL1" "active"
# Check new file.
log_must grep '^whiteout$' new
# Check that the whiteout is actually a {0,0} char device.
log_must grep '^character special file:0:0$' <<<"$(stat -c '%F:%t:%T' whiteout)"

# The feature flag must remain active until a clean export.
check_feature_flag "feature@rename_whiteout" "$TESTPOOL1" "active"
log_must zpool export "$TESTPOOL1"
log_must zpool import "$TESTPOOL1"
check_feature_flag "feature@rename_whiteout" "$TESTPOOL1" "enabled"

log_pass "ZFS supports RENAME_WHITEOUT as expected."
