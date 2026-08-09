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
# Copyright (c) 2012, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zfs_set/zfs_set_common.kshlib

#
# DESCRIPTION:
#	If ZFS is currently managing the file system but it is currently unmounted,
#	and the mountpoint property is changed, the file system should be mounted
#	if it is a valid mountpoint and canmount allows to mount, otherwise it
#	should not be mounted.
#
# STRATEGY:
# 1. Setup a pool and create fs, ctr within it.
# 2. Unmount that dataset
# 2. Change the mountpoint to the valid mountpoint value.
# 3. Check the file system remains unmounted.
#

verify_runnable "both"

export TESTDIR_NOTEXISTING=${TEST_BASE_DIR%%/}/testdir_notexisting$$

set -A dataset "$TESTPOOL/$TESTFS" "$TESTPOOL/$TESTCTR"

set -A values "$TESTDIR2" "$TESTDIR_NOTEXISTING"

function cleanup
{
	log_must zfs set mountpoint=$old_ctr_mpt $TESTPOOL/$TESTCTR
	log_must zfs set mountpoint=$old_fs_mpt $TESTPOOL/$TESTFS
	log_must zfs mount -a
	[[ -d $TESTDIR2 ]] && log_must rm -r $TESTDIR2
	[[ -d $TESTDIR_NOTEXISTING ]] && log_must rm -r $TESTDIR_NOTEXISTING
}

log_assert "Setting a valid mountpoint for an unmounted file system, \
	it gets mounted."
log_onexit cleanup

old_fs_mpt=$(get_prop mountpoint $TESTPOOL/$TESTFS)
old_ctr_mpt=$(get_prop mountpoint $TESTPOOL/$TESTCTR)

if [[ ! -d $TESTDIR2 ]]; then
	log_must mkdir $TESTDIR2
fi

typeset -i i=0
typeset -i j=0
while (( i < ${#dataset[@]} )); do
	j=0
	if ismounted ${dataset[i]} ; then
		log_must zfs unmount ${dataset[i]}
	fi
	log_mustnot ismounted ${dataset[i]}
	while (( j < ${#values[@]} )); do
		set_n_check_prop "${values[j]}" "mountpoint" \
			"${dataset[i]}"
		if [ "${dataset[i]}" = "$TESTPOOL/$TESTFS" ]; then
			log_must ismounted ${dataset[i]}
		else
			log_mustnot ismounted ${dataset[i]}
		fi
		(( j += 1 ))
	done
	cleanup
	(( i += 1 ))
done

log_pass "Setting a valid mountpoint for an unmounted file system, \
	it remains unmounted."
