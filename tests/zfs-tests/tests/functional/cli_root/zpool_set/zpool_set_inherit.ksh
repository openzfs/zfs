#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or https://opensource.org/licenses/CDDL-1.0.
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
# Copyright 2026, Klara, Inc. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#
# zpool set can set the failfast property to 'inherit'
#
# STRATEGY:
# 1. Create a pool
# 2. Verify that we can set 'failfast' to various values, including inherit
# 3. Verify that the root vdev cannot be set to inherit
#

verify_runnable "global"

function cleanup
{
	destroy_pool $TESTPOOL1
	rm -f $FILEVDEV1 $FILEVDEV2 $FILEVDEV3
}

function get_failfast
{
	zpool get -H -o value failfast $TESTPOOL1 $@
}

log_onexit cleanup

log_assert "zpool set can configure 'failfast' property to inherit"
FILEVDEV1="$TEST_BASE_DIR/zpool_set_inherit1.$$.dat"
FILEVDEV2="$TEST_BASE_DIR/zpool_set_inherit2.$$.dat"
FILEVDEV3="$TEST_BASE_DIR/zpool_set_inherit3.$$.dat"

log_must truncate -s $MINVDEVSIZE $FILEVDEV1
log_must truncate -s $MINVDEVSIZE $FILEVDEV2
log_must truncate -s $MINVDEVSIZE $FILEVDEV3

log_must zpool create -f $TESTPOOL1 $FILEVDEV1 mirror $FILEVDEV2 $FILEVDEV3
failfast=$(get_failfast $FILEVDEV1)
[[ "$failfast" == "inherit" ]] || log_fail "incorrect failfast value: $failfast"

log_must zpool set failfast=on $TESTPOOL1 $FILEVDEV1
failfast=$(get_failfast $FILEVDEV1)
[[ "$failfast" == "on" ]] || log_fail "incorrect failfast value: $failfast"

log_must zpool set failfast=off $TESTPOOL1 $FILEVDEV1
failfast=$(get_failfast $FILEVDEV1)
[[ "$failfast" == "off" ]] || log_fail "incorrect failfast value: $failfast"

log_must zpool set failfast=inherit $TESTPOOL1 $FILEVDEV1

failfast=$(get_failfast $FILEVDEV2)
[[ "$failfast" == "inherit" ]] || log_fail "incorrect failfast value: $failfast"

log_must zpool set failfast=on $TESTPOOL1 $FILEVDEV2
failfast=$(get_failfast $FILEVDEV2)
[[ "$failfast" == "on" ]] || log_fail "incorrect failfast value: $failfast"

log_must zpool set failfast=off $TESTPOOL1 $FILEVDEV2
failfast=$(get_failfast $FILEVDEV2)
[[ "$failfast" == "off" ]] || log_fail "incorrect failfast value: $failfast"

log_must zpool set failfast=inherit $TESTPOOL1 $FILEVDEV2

failfast=$(get_failfast mirror-1)
[[ "$failfast" == "inherit" ]] || log_fail "incorrect failfast value: $failfast"

log_must zpool set failfast=on $TESTPOOL1 mirror-1
failfast=$(get_failfast mirror-1)
[[ "$failfast" == "on" ]] || log_fail "incorrect failfast value: $failfast"

log_must zpool set failfast=off $TESTPOOL1 mirror-1
failfast=$(get_failfast mirror-1)
[[ "$failfast" == "off" ]] || log_fail "incorrect failfast value: $failfast"

log_must zpool set failfast=inherit $TESTPOOL1 mirror-1

failfast=$(get_failfast root)
[[ "$failfast" == "on" ]] || log_fail "incorrect failfast value: $failfast"

log_must zpool set failfast=off $TESTPOOL1 root
failfast=$(get_failfast root)
[[ "$failfast" == "off" ]] || log_fail "incorrect failfast value: $failfast"

log_mustnot zpool set failfast=inherit $TESTPOOL1 root


log_pass "zpool set can configure 'failfast' property to inherit"
