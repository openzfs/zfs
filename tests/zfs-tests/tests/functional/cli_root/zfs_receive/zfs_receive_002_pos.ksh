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
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/cli_root/cli_common.kshlib

#
# DESCRIPTION:
#	Verifying 'zfs receive <volume>' works.
#
# STRATEGY:
#	1. Fill in volume with some data
#	2. Create full and incremental send stream
#	3. Restore the send stream
#	4. Verify the restoring results.
#

verify_runnable "global"

function cleanup
{
	typeset -i i=0
	typeset ds

	while (( i < ${#orig_snap[*]} )); do
		snapexists ${rst_snap[$i]} && destroy_dataset ${rst_snap[$i]} -f
		snapexists ${orig_snap[$i]} && destroy_dataset ${orig_snap[$i]} -f
		[[ -e ${bkup[$i]} ]] && \
			log_must rm -rf ${bkup[$i]}

		(( i = i + 1 ))
	done

	for ds in $rst_vol $rst_root; do
		datasetexists $ds && destroy_dataset $ds -Rf
	done
}

log_assert "Verifying 'zfs receive <volume>' works."
log_onexit cleanup

set -A orig_snap "$TESTPOOL/$TESTVOL@init_snap" "$TESTPOOL/$TESTVOL@inc_snap"
set -A bkup "$TEST_BASE_DIR/fullbkup" "$TEST_BASE_DIR/incbkup"
rst_root=$TESTPOOL/rst_ctr
rst_vol=$rst_root/$TESTVOL
set -A rst_snap "${rst_vol}@init_snap" "${rst_vol}@inc_snap"

#
# Preparations for testing
#
log_must zfs create $rst_root
[[ ! -d $TESTDIR1 ]] && \
	log_must mkdir -p $TESTDIR1
log_must zfs set mountpoint=$TESTDIR1 $rst_root

typeset -i i=0
while (( i < ${#orig_snap[*]} )); do
	log_must zfs snapshot ${orig_snap[$i]}
	if (( i < 1 )); then
		log_must eval "zfs send ${orig_snap[$i]} > ${bkup[$i]}"
	else
		log_must eval "zfs send -i ${orig_snap[(( i - 1 ))]} \
				${orig_snap[$i]} > ${bkup[$i]}"
	fi

	(( i = i + 1 ))
done

i=0
while (( i < ${#bkup[*]} )); do
	log_must eval "zfs receive $rst_vol < ${bkup[$i]}"
	! datasetexists $rst_vol || ! snapexists ${rst_snap[$i]} && \
		log_fail "Restoring volume fails."

	(( i = i + 1 ))
done

log_pass "Verifying 'zfs receive <volume>' succeeds."
