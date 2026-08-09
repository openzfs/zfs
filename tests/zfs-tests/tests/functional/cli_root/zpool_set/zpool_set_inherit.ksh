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
