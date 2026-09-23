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
#
. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_initialize/zpool_initialize.kshlib

#
# DESCRIPTION:
# Cancelling and suspending initialize doesn't work if not all specified vdevs
# are being initialized.
#
# STRATEGY:
# 1. Create a three-disk pool.
# 2. Start initializing and verify that initializing is active.
# 3. Try to cancel and suspend initializing on the non-initializing disks.
# 4. Try to re-initialize the currently initializing disk.
# 5. Repeat for other VDEVs
#

DISK1=${DISKS%% *}
DISK2="$(echo $DISKS | cut -d' ' -f2)"
DISK3="$(echo $DISKS | cut -d' ' -f3)"

for type in "" "anymirror2"; do

	log_must zpool list -v
	log_must zpool create -O compress=off -f $TESTPOOL $type $DISK1 $DISK2 $DISK3
	if [[ "$type" == "anymirror2" ]]; then
		log_must file_write -o create -f /$TESTPOOL/f1 -b 1048576 -c 2000 -d Z
		log_must zpool sync
		log_must rm /$TESTPOOL/f1
	fi
	log_must zpool initialize $TESTPOOL $DISK1

	[[ -z "$(initialize_progress $TESTPOOL $DISK1)" ]] && \
	    log_fail "Initialize did not start"

	log_mustnot zpool initialize -c $TESTPOOL $DISK2
	log_mustnot zpool initialize -c $TESTPOOL $DISK2 $DISK3

	log_mustnot zpool initialize -s $TESTPOOL $DISK2
	log_mustnot zpool initialize -s $TESTPOOL $DISK2 $DISK3

	log_mustnot zpool initialize $TESTPOOL $DISK1

	poolexists $TESTPOOL && destroy_pool $TESTPOOL

done

log_pass "Nonsensical initialize operations fail"
