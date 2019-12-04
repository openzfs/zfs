#!/bin/ksh -p
#
# CDDL HEADER START
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# http://www.illumos.org/license/CDDL.
#
# CDDL HEADER END
#

#
# Copyright (c) 2019 by Tim Chase. All rights reserved.
# Copyright (c) 2019 Lawrence Livermore National Security, LLC.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_trim/zpool_trim.kshlib

#
# DESCRIPTION:
# Trimming can be performed multiple times
#
# STRATEGY:
# 1. Create a pool with a single disk.
# 2. Trim the entire pool.
# 3. Verify trimming is reset (status, offset, and action date).
# 4. Repeat steps 2 and 3 with the existing pool.
#

DISK1=${DISKS%% *}

log_must zpool create -f $TESTPOOL $DISK1

typeset action_date="none"
for n in {1..3}; do
	log_must zpool trim -r 2G $TESTPOOL
	log_mustnot eval "trim_prog_line $TESTPOOL $DISK1 | grep complete"

	[[ "$(trim_progress $TESTPOOL $DISK1)" -lt "100" ]] ||
	    log_fail "Trimming progress wasn't reset"

	new_action_date="$(trim_prog_line $TESTPOOL $DISK1 | \
	    sed 's/.*ed at \(.*\)).*/\1/g')"
	[[ "$action_date" != "$new_action_date" ]] ||
		log_fail "Trimming action date wasn't reset"
	action_date=$new_action_date

	while [[ "$(trim_progress $TESTPOOL $DISK1)" -lt "100" ]]; do
		progress="$(trim_progress $TESTPOOL $DISK1)"
		sleep 0.5
		[[ "$progress" -le "$(trim_progress $TESTPOOL $DISK1)" ]] ||
		    log_fail "Trimming progress regressed"
	done

	log_must eval "trim_prog_line $TESTPOOL $DISK1 | grep complete"
	sleep 1
done

log_pass "Trimming multiple times performs as expected"
