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
# Copyright (c) 2016 by Delphix. All rights reserved.
#
. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_initialize/zpool_initialize.kshlib

#
# DESCRIPTION:
# Initializing automatically resumes across import/export.
#
# STRATEGY:
# 1. Create a pool.
# 2. Start initializing and verify that initializing is active.
# 3. Export the pool.
# 4. Import the pool.
# 5. Verify that initializing resumes and progress does not regress.
# 6. Suspend initializing.
# 7. Repeat steps 3-4.
# 8. Verify that progress does not regress but initializing is still suspended.
# 9. Repeat for other VDEV types.
#

DISK1="$(echo $DISKS | cut -d' ' -f1)"
DISK2="$(echo $DISKS | cut -d' ' -f2)"

for type in "" "anyraid1"; do
	if [[ "$type" = "" ]]; then
		VDEVS="$DISK1"
	elif [[ "$type" = "anyraid1" ]]; then
		VDEVS="$DISK1 $DISK2"
	fi

	log_must zpool create -f $TESTPOOL $type $VDEVS
	log_must zpool initialize $TESTPOOL

	sleep 2

	progress="$(initialize_progress $TESTPOOL $DISK1)"
	[[ -z "$progress" ]] && log_fail "Initializing did not start"

	log_must zpool export $TESTPOOL
	log_must zpool import $TESTPOOL

	new_progress="$(initialize_progress $TESTPOOL $DISK1)"
	[[ -z "$new_progress" ]] && log_fail "Initializing did not restart after import"
	[[ "$progress" -le "$new_progress" ]] || \
	    log_fail "Initializing lost progress after import"
	log_mustnot eval "initialize_prog_line $TESTPOOL $DISK1 | grep suspended"

	log_must zpool initialize -s $TESTPOOL $DISK1
	action_date="$(initialize_prog_line $TESTPOOL $DISK1 | \
	    sed 's/.*ed at \(.*\)).*/\1/g')"
	log_must zpool export $TESTPOOL
	log_must zpool import $TESTPOOL
	new_action_date=$(initialize_prog_line $TESTPOOL $DISK1 | \
	    sed 's/.*ed at \(.*\)).*/\1/g')
	[[ "$action_date" != "$new_action_date" ]] && \
	    log_fail "Initializing action date did not persist across export/import"

	[[ "$new_progress" -le "$(initialize_progress $TESTPOOL $DISK1)" ]] || \
	        log_fail "Initializing lost progress after import"

	log_must eval "initialize_prog_line $TESTPOOL $DISK1 | grep suspended"

	poolexists $TESTPOOL && destroy_pool $TESTPOOL
done

log_pass "Initializing retains state as expected across export/import"
