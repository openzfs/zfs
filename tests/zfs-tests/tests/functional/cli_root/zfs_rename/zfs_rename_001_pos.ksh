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
#       'zfs rename' should successfully rename valid datasets.
#       As a sub-assertion we check to ensure the datasets that can
#       be mounted are mounted.
#
# STRATEGY:
#       1. Given a file system, snapshot and volume.
#       2. Rename each dataset object to a new name.
#       3. Verify that only the new name is displayed by zfs list.
#       4. Verify mountable datasets are mounted.
#
###############################################################################

verify_runnable "both"

set -A dataset "$TESTPOOL/$TESTFS@snapshot" "$TESTPOOL/$TESTFS1" \
   "$TESTPOOL/$TESTCTR/$TESTFS1" "$TESTPOOL/$TESTCTR1" \
    "$TESTPOOL/$TESTVOL" "$TESTPOOL/$TESTFS-clone"
set -A mountable "$TESTPOOL/$TESTFS1-new" "$TESTPOOL/$TESTFS@snapshot-new" \
    "$TESTPOOL/$TESTCTR/$TESTFS1-new" "$TESTPOOL/$TESTFS-clone-new"

#
# cleanup defined in zfs_rename.kshlib
#
log_onexit cleanup

log_assert "'zfs rename' should successfully rename valid datasets"

additional_setup

typeset -i i=0
while (( i < ${#dataset[*]} )); do
	rename_dataset ${dataset[i]} ${dataset[i]}-new

	((i = i + 1))
done

log_note "Verify mountable datasets are mounted in their new namespace."
typeset mtpt
i=0
while (( i < ${#mountable[*]} )); do
	# Snapshot have no mountpoint
	if [[ ${mountable[i]} != *@* ]]; then
		log_must mounted ${mountable[i]}
		mtpt=$(get_prop mountpoint ${mountable[i]})
	else
		mtpt=$(snapshot_mountpoint ${mountable[i]})
	fi

	if ! cmp_data $DATA $mtpt/$TESTFILE0 ; then
		log_fail "$mtpt/$TESTFILE0 gets corrupted after rename operation."
	fi

	((i = i + 1))
done

#verify the data integrity in zvol
if is_global_zone; then
	log_must eval "dd if=${VOL_R_PATH}-new of=$VOLDATA bs=$BS count=$CNT >/dev/null 2>&1"
	if ! cmp_data $VOLDATA $DATA ; then
		log_fail "$VOLDATA gets corrupted after rename operation."
	fi
fi

# rename back fs
typeset -i i=0
while ((i < ${#dataset[*]} )); do
	if datasetexists ${dataset[i]}-new ; then
                log_must zfs rename ${dataset[i]}-new ${dataset[i]}
	fi
        ((i = i + 1))
done

log_pass "'zfs rename' successfully renamed each dataset type."
