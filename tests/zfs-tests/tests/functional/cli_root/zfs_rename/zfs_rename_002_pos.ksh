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

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zfs_rename/zfs_rename.kshlib

#
# DESCRIPTION:
#       'zfs rename' should successfully be capable of renaming
#       valid datasets back and forth multiple times.
#
# STRATEGY:
#       1. Given a file system, snapshot and volume.
#       2. Rename each dataset object to a new name.
#       3. Rename each dataset back to its original name.
#       4. Repeat steps 2 and 3 multiple times.
#       5. Verify that the correct name is displayed by zfs list.
#
###############################################################################

verify_runnable "both"

set -A dataset "$TESTPOOL/$TESTFS@snapshot" "$TESTPOOL/$TESTFS1" \
   "$TESTPOOL/$TESTCTR/$TESTFS1" "$TESTPOOL/$TESTCTR1" \
    "$TESTPOOL/$TESTVOL" "$TESTPOOL/$TESTFS-clone"

#
# cleanup defined in zfs_rename.kshlib
#
log_onexit cleanup

log_assert "'zfs rename' should successfully rename valid datasets"

additional_setup

typeset -i i=0
typeset -i iters=10

while ((i < ${#dataset[*]} )); do
	j=0
	while ((j < iters )); do
		rename_dataset ${dataset[i]} ${dataset[i]}-new
		rename_dataset ${dataset[i]}-new ${dataset[i]}

		((j = j + 1))
	done

	if [[ ${dataset[i]} == *@* ]]; then
		data=$(snapshot_mountpoint ${dataset[i]})/$TESTFILE0
	elif [[ ${dataset[i]} == "$TESTPOOL/$TESTVOL" ]] && is_global_zone; then
		log_must eval "dd if=$VOL_R_PATH of=$VOLDATA bs=$BS count=$CNT >/dev/null 2>&1"
		data=$VOLDATA
	else
		data=$(get_prop mountpoint ${dataset[i]})/$TESTFILE0
	fi

	if ! cmp_data $DATA $data; then
		log_fail "$data gets corrupted after $iters times rename operations."
	fi

	((i = i + 1))
done

log_pass "'zfs rename' renamed each dataset type multiple times as expected."
