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
# Copyright (c) 2025 by Klara, Inc.
#
. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_initialize/zpool_initialize.kshlib

#
# DESCRIPTION:
# Initializing automatically resumes across offline/online.
#
# STRATEGY:
# 1. Create a pool with a two-way mirror.
# 2. Start initializing one of the disks and verify that initializing is active.
# 3. Offline the disk.
# 4. Online the disk.
# 5. Verify that initializing resumes and progress does not regress.
# 6. Suspend initializing.
# 7. Repeat steps 3-4 and verify that initializing does not resume.
# 8. Repeat the scenario for other VDEVs
#

DISK1=${DISKS%% *}
DISK2="$(echo $DISKS | cut -d' ' -f2)"
DISK3="$(echo $DISKS | cut -d' ' -f3)"

log_onexit_push zpool status -v

for type in "mirror" "anymirror1"; do

	if [[ "$type" == "mirror" ]]; then
		log_must zpool create -f $TESTPOOL $type $DISK1 $DISK2
	else
		log_must zpool create -f $TESTPOOL $type $DISK1 $DISK2 $DISK3
		log_must file_write -o create -f /$TESTPOOL/f1 -b 1048576 -c 400 -d R
		log_must zpool sync
		log_must rm /$TESTPOOL/f1
		log_must zpool sync
	fi
	log_must zinject -D 10:1 -d $DISK1 -T write $TESTPOOL
	log_must zpool initialize $TESTPOOL $DISK1

	log_must zpool offline $TESTPOOL $DISK1

	progress="$(initialize_progress $TESTPOOL $DISK1)"
	[[ -z "$progress" ]] && log_fail "Initializing did not start"

	log_must zpool online $TESTPOOL $DISK1

	new_progress="$(initialize_progress $TESTPOOL $DISK1)"
	[[ -z "$new_progress" ]] && \
	    log_fail "Initializing did not restart after onlining"
	[[ "$progress" -le "$new_progress" ]] || \
	    log_fail "Initializing lost progress after onlining"
	log_mustnot eval "initialize_prog_line $TESTPOOL $DISK1 | grep suspended"

	log_must zpool initialize -s $TESTPOOL $DISK1
	log_must zinject -c all
	action_date="$(initialize_prog_line $TESTPOOL $DISK1 | \
	    sed 's/.*ed at \(.*\)).*/\1/g')"
	log_must zpool offline $TESTPOOL $DISK1
	log_must zpool online $TESTPOOL $DISK1
	new_action_date=$(initialize_prog_line $TESTPOOL $DISK1 | \
	    sed 's/.*ed at \(.*\)).*/\1/g')
	[[ "$action_date" != "$new_action_date" ]] && \
	    log_fail "Initializing action date did not persist across offline/online"
	log_must eval "initialize_prog_line $TESTPOOL $DISK1 | grep suspended"

	poolexists $TESTPOOL && destroy_pool $TESTPOOL

done

log_pass "Initializing performs as expected across offline/online"
