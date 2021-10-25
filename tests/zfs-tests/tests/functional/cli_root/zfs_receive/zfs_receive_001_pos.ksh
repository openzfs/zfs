#!/bin/ksh -p
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
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
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2012, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/cli_root/cli_common.kshlib

#
# DESCRIPTION:
#	Verifying 'zfs receive [<filesystem|snapshot>] -d <filesystem>' works.
#
# STRATEGY:
#	1. Fill in fs with some data
#	2. Create full and incremental send stream
#	3. Receive the send stream
#	4. Verify the restoring results.
#

verify_runnable "both"

function cleanup
{
	typeset -i i=0

	datasetexists $rst_root && destroy_dataset $rst_root -Rf
	while (( i < 2 )); do
		snapexists ${orig_snap[$i]} && destroy_dataset ${orig_snap[$i]} -f
		log_must rm -f ${bkup[$i]}

		(( i = i + 1 ))
	done

	log_must rm -rf $TESTDIR1
}

function recreate_root
{
	datasetexists $rst_root && destroy_dataset $rst_root -Rf
	if [[ -d $TESTDIR1 ]] ; then
		log_must rm -rf $TESTDIR1
	fi
	log_must zfs create $rst_root
	log_must zfs set mountpoint=$TESTDIR1 $rst_root
}

log_assert "Verifying 'zfs receive [<filesystem|snapshot>] -d <filesystem>' works."
log_onexit cleanup

typeset datasets="$TESTPOOL/$TESTFS $TESTPOOL"
set -A bkup "$TEST_BASE_DIR/fullbkup" "$TEST_BASE_DIR/incbkup"
orig_sum=""
rst_sum=""
rst_root=$TESTPOOL/rst_ctr
rst_fs=${rst_root}/$TESTFS

for orig_fs in $datasets ; do
	#
	# Preparations for testing
	#
	recreate_root

	set -A orig_snap "${orig_fs}@init_snap" "${orig_fs}@inc_snap"
	typeset mntpnt=$(get_prop mountpoint ${orig_fs})
	set -A orig_data "${mntpnt}/$TESTFILE1" "${mntpnt}/$TESTFILE2"

	typeset relative_path=""
	if [[ ${orig_fs} == *"/"* ]]; then
		relative_path=${orig_fs#*/}
	fi

	typeset leaf_fs=${rst_root}/${relative_path}
	leaf_fs=${leaf_fs%/}
	rst_snap=${leaf_fs}@snap

	set -A rst_snap "$rst_root/$TESTFS@init_snap" "$rst_root/$TESTFS@inc_snap"
	set -A rst_snap2 "${leaf_fs}@init_snap" "${leaf_fs}@inc_snap"
	set -A rst_data "$TESTDIR1/$TESTFS/$TESTFILE1" "$TESTDIR1/$TESTFS/$TESTFILE2"
	set -A rst_data2 "$TESTDIR1/${relative_path}/$TESTFILE1" "$TESTDIR1/${relative_path}/$TESTFILE2"

	typeset -i i=0
	while (( i < ${#orig_snap[*]} )); do
		file_write -o create -f ${orig_data[$i]} -b 512 \
		    -c 8 >/dev/null 2>&1
		(( $? != 0 )) && \
			log_fail "Writing data into zfs filesystem fails."
		log_must zfs snapshot ${orig_snap[$i]}
		if (( i < 1 )); then
			log_must eval "zfs send ${orig_snap[$i]} > ${bkup[$i]}"
		else
			log_must eval "zfs send -i ${orig_snap[(( i - 1 ))]} \
				${orig_snap[$i]} > ${bkup[$i]}"
		fi

		(( i = i + 1 ))
	done

	log_note "Verifying 'zfs receive <filesystem>' works."
	i=0
	while (( i < ${#bkup[*]} )); do
		if (( i > 0 )); then
			log_must zfs rollback ${rst_snap[0]}
		fi
		log_must eval "zfs receive $rst_fs < ${bkup[$i]}"
		snapexists ${rst_snap[$i]} || \
			log_fail "Restoring filesystem fails. ${rst_snap[$i]} not exist"
		compare_cksum ${orig_data[$i]} ${rst_data[$i]}

		(( i = i + 1 ))
	done

	log_must zfs destroy -Rf $rst_fs

	log_note "Verifying 'zfs receive <snapshot>' works."
	i=0
	while (( i < ${#bkup[*]} )); do
		if (( i > 0 )); then
			log_must zfs rollback ${rst_snap[0]}
		fi
		log_must eval "zfs receive ${rst_snap[$i]} <${bkup[$i]}"
		snapexists ${rst_snap[$i]} || \
			log_fail "Restoring filesystem fails. ${rst_snap[$i]} not exist"
		compare_cksum ${orig_data[$i]} ${rst_data[$i]}

		(( i = i + 1 ))
	done

	log_must zfs destroy -Rf $rst_fs

	log_note "Verifying 'zfs receive -d <filesystem>' works."

	i=0
	while (( i < ${#bkup[*]} )); do
		if (( i > 0 )); then
			log_must zfs rollback ${rst_snap2[0]}
		fi
		log_must eval "zfs receive -d -F $rst_root <${bkup[$i]}"
		snapexists ${rst_snap2[$i]} || \
			log_fail "Restoring filesystem fails. ${rst_snap2[$i]} not exist"
		compare_cksum ${orig_data[$i]} ${rst_data2[$i]}

		(( i = i + 1 ))
	done

	cleanup
done

log_pass "Verifying 'zfs receive [<filesystem|snapshot>] -d <filesystem>' succeeds."
