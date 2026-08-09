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
# Copyright (c) 2016 by Delphix. All rights reserved.
# Copyright (C) 2023 Lawrence Livermore National Security, LLC.
#
. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_initialize/zpool_initialize.kshlib

#
# DESCRIPTION:
# Starting, stopping, uninitializing, and restart an initialize works.
#
# STRATEGY:
# 1. Create a one-disk pool.
# 2. Verify uninitialize succeeds for uninitialized pool.
# 3. Verify pool wide cancel|suspend + uninit
#   a. Start initializing and verify that initializing is active.
#   b. Verify uninitialize fails when actively initializing.
#   c. Cancel or suspend initializing and verify that initializing is not active.
#   d. Verify uninitialize succeeds after being cancelled.
# 4. Verify per-disk cancel|suspend + uninit
#

DISK1="$(echo $DISKS | cut -d' ' -f1)"
DISK2="$(echo $DISKS | cut -d' ' -f2)"
DISK3="$(echo $DISKS | cut -d' ' -f3)"

function status_check # pool disk1-state disk2-state disk3-state
{
        typeset pool="$1"
        typeset disk1_state="$2"
        typeset disk2_state="$3"
        typeset disk3_state="$4"

	state=$(zpool status -i "$pool" | grep "$DISK1" | grep "$disk1_state")
        if [[ -z "$state" ]]; then
		log_fail "DISK1 state; expected='$disk1_state' got '$state'"
	fi

	state=$(zpool status -i "$pool" | grep "$DISK2" | grep "$disk2_state")
        if [[ -z "$state" ]]; then
		log_fail "DISK2 state; expected='$disk2_state' got '$state'"
	fi

	state=$(zpool status -i "$pool" | grep "$DISK3" | grep "$disk3_state")
        if [[ -z "$state" ]]; then
		log_fail "DISK3 state; expected='$disk3_state' got '$state'"
	fi
}

function status_check_all # pool disk-state
{
        typeset pool="$1"
        typeset disk_state="$2"

	status_check "$pool" "$disk_state" "$disk_state" "$disk_state"
}

# 1. Create a one-disk pool.
log_must zpool create -f $TESTPOOL $DISK1 $DISK2 $DISK3
status_check_all $TESTPOOL "uninitialized"

# 2. Verify uninitialize succeeds for uninitialized pool.
log_must zpool initialize -u $TESTPOOL
status_check_all $TESTPOOL "uninitialized"

# 3. Verify pool wide cancel + uninit
log_must zpool initialize $TESTPOOL
status_check_all $TESTPOOL "[[:digit:]]* initialized"

log_mustnot zpool initialize -u $TESTPOOL
status_check_all $TESTPOOL "[[:digit:]]* initialized"

log_must zpool initialize -c $TESTPOOL
status_check_all $TESTPOOL "uninitialized"

log_must zpool initialize -u $TESTPOOL
status_check_all $TESTPOOL "uninitialized"

# 3. Verify pool wide suspend + uninit
log_must zpool initialize $TESTPOOL
status_check_all $TESTPOOL "[[:digit:]]* initialized"

log_mustnot zpool initialize -u $TESTPOOL
status_check_all $TESTPOOL "[[:digit:]]* initialized"

log_must zpool initialize -s $TESTPOOL
status_check_all $TESTPOOL "suspended"

log_must zpool initialize -u $TESTPOOL
status_check_all $TESTPOOL "uninitialized"

# 4. Verify per-disk cancel|suspend + uninit
log_must zpool initialize $TESTPOOL
status_check_all $TESTPOOL "[[:digit:]]* initialized"

log_must zpool initialize -c $TESTPOOL $DISK1
log_must zpool initialize -s $TESTPOOL $DISK2
log_mustnot zpool initialize -u $TESTPOOL $DISK3
status_check $TESTPOOL "uninitialized" "suspended" "[[:digit:]]* initialized"

log_must zpool initialize -u $TESTPOOL $DISK1
status_check $TESTPOOL "uninitialized" "suspended" "[[:digit:]]* initialized"

log_must zpool initialize -u $TESTPOOL $DISK2
status_check $TESTPOOL "uninitialized" "uninitialized" "[[:digit:]]* initialized"

log_must zpool initialize $TESTPOOL $DISK1
status_check $TESTPOOL "[[:digit:]]* initialized" "uninitialized" "[[:digit:]]* initialized"

log_must zpool initialize $TESTPOOL $DISK2
status_check_all $TESTPOOL "[[:digit:]]* initialized"

log_must zpool initialize -s $TESTPOOL
status_check_all $TESTPOOL "suspended"

log_must zpool initialize -u $TESTPOOL $DISK1 $DISK2 $DISK3
status_check_all $TESTPOOL "uninitialized"

log_pass "Initialize start + cancel/suspend + uninit + start works"
