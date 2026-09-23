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

. "$STF_SUITE"/include/libtest.shlib
. "$STF_SUITE"/tests/functional/replacement/rebuild_test.kshlib

#
# DESCRIPTION:
#	Expected failures on old dRAID devices do not fail healthy rebuilds.
#
# STRATEGY:
#	1. Rebuild a faulted device onto a distributed spare in a fixed layout.
#	   Writes through the spare may reach the unavailable original device;
#	   its DTL is maintained without failing the spare's reconstruction.
#	2. Require zero rebuild errors and completed replacement before any
#	   healing or scrub is allowed to hide a failed retirement decision.
#

verify_runnable "global"
log_assert "Healthy dRAID rebuilds retire the replacement's missing ranges"
rebuild_test_init

rebuild_test_create 10 draid2:1d:10c:2s
log_must zpool offline -f "$TESTPOOL1" "$rebuild_dir/disk-0"
log_must zpool replace -s "$TESTPOOL1" \
    "$rebuild_dir/disk-0" draid2-0-0
rebuild_test_completed 0
log_must rebuild_test_wait replace
log_must check_vdev_state "$TESTPOOL1" "$rebuild_dir/disk-0" "FAULTED"
log_must check_hotspare_state "$TESTPOOL1" draid2-0-0 "INUSE"
rebuild_test_finish

log_pass "Healthy dRAID rebuilds completed without false repair failures"
