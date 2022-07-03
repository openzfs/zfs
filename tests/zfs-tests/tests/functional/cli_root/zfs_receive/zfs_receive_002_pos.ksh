#!/bin/ksh -p
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
