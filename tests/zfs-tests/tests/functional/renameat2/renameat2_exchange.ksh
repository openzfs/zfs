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

log_assert "ZFS supports RENAME_EXCHANGE."
log_onexit cleanup

check_feature_flag "feature@rename_exchange" "$TESTPOOL1" "enabled"

cd $TESTDIR
echo "foo" > foo
echo "bar" > bar

# Self-exchange is a no-op.
log_must renameat2 -x foo foo
log_must grep '^foo$' foo

# Basic exchange.
log_must renameat2 -x foo bar
check_feature_flag "feature@rename_exchange" "$TESTPOOL1" "active"
log_must grep '^bar$' foo
log_must grep '^foo$' bar

# And exchange back.
log_must renameat2 -x foo bar
check_feature_flag "feature@rename_exchange" "$TESTPOOL1" "active"
log_must grep '^foo$' foo
log_must grep '^bar$' bar

# Exchange with a bad path should fail.
log_mustnot renameat2 -x bar baz

# The feature flag must remain active until a clean export.
check_feature_flag "feature@rename_exchange" "$TESTPOOL1" "active"
log_must zpool export "$TESTPOOL1"
log_must zpool import "$TESTPOOL1"
check_feature_flag "feature@rename_exchange" "$TESTPOOL1" "enabled"

log_pass "ZFS supports RENAME_EXCHANGE as expected."
