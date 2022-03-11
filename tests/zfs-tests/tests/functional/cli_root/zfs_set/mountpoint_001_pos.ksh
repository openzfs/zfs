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
# Setting valid mountpoint to filesystem, it is successful.
# Whatever is set to volume, it is failed.
# 'zfs set mountpoint=<path>|legacy|none <fs|ctr|vol>'
#
# STRATEGY:
# 1. Setup a pool and create fs, ctr within it.
# 2. Loop all the valid mountpoint value.
# 3. Check the return value.
#

verify_runnable "both"

export TESTDIR_NOTEXISTING=${TEST_BASE_DIR%%/}/testdir_notexisting$$

if is_global_zone ; then
	set -A dataset \
		"$TESTPOOL/$TESTFS" "$TESTPOOL/$TESTCTR" "$TESTPOOL/$TESTVOL"
else
	set -A dataset "$TESTPOOL/$TESTFS" "$TESTPOOL/$TESTCTR"
fi

set -A values "$TESTDIR2" "legacy" "none" "$TESTDIR_NOTEXISTING"

function cleanup
{
	log_must zfs set mountpoint=$old_ctr_mpt $TESTPOOL/$TESTCTR
	log_must zfs set mountpoint=$old_fs_mpt $TESTPOOL/$TESTFS
	[[ -d $TESTDIR2 ]] && log_must rm -r $TESTDIR2
	[[ -d $TESTDIR_NOTEXISTING ]] && log_must rm -r $TESTDIR_NOTEXISTING
}

log_assert "Setting a valid mountpoint to file system, it must be successful."
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
	while (( j < ${#values[@]} )); do
		if [[ ${dataset[i]} == "$TESTPOOL/$TESTVOL" ]]; then
			set_n_check_prop "${values[j]}" "mountpoint" \
				"${dataset[i]}" "false"
		else
			set_n_check_prop "${values[j]}" "mountpoint" \
				"${dataset[i]}"
		fi
		(( j += 1 ))
	done
	cleanup
	(( i += 1 ))
done

log_pass "Setting mountpoint to filesystem pass."
