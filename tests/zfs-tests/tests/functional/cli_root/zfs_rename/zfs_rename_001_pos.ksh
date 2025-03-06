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
