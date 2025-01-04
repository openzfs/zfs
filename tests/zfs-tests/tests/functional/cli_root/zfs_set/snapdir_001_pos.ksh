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

. $STF_SUITE/tests/functional/cli_root/zfs_set/zfs_set_common.kshlib

#
# DESCRIPTION:
# Setting a valid snapdir on a dataset, it should be successful.
#
# STRATEGY:
# 1. Create pool, then create filesystem and volume within it.
# 2. Create a snapshot for each dataset.
# 3. Setting different valid snapdir to each dataset.
# 4. Check the return value and make sure it is 0.
# 5. Verify .zfs directory is hidden|visible according to the snapdir setting.
#

verify_runnable "both"

function cleanup
{
	for dataset in $all_datasets; do
		snapexists ${dataset}@snap && destroy_dataset ${dataset}@snap
	done
}

function verify_snapdir_visible # $1 dataset, $2 hidden|visible
{
	typeset dataset=$1
	typeset value=$2
	typeset mtpt=$(get_prop mountpoint $dataset)

	# $mtpt/.zfs always actually exists so [ -d $mtpt/.zfs ] is always true
	if ls -a $mtpt | grep -xFq .zfs; then
		[ $value = "visible" ]
	else
		[ $value != "visible" ]
	fi
}


typeset all_datasets

if is_global_zone ; then
	all_datasets="$TESTPOOL $TESTPOOL/$TESTFS $TESTPOOL/$TESTVOL"
else
	all_datasets="$TESTPOOL $TESTPOOL/$TESTFS"
fi

log_onexit cleanup

for dataset in $all_datasets; do
	log_must zfs snapshot ${dataset}@snap
done

log_assert "Setting a valid snapdir property on a dataset succeeds."

for dataset in $all_datasets; do
	for value in hidden visible; do
		if [ "$dataset" = "$TESTPOOL/$TESTVOL" ]; then
			set_n_check_prop "$value" "snapdir" \
				"$dataset" "false"
		else
			set_n_check_prop "$value" "snapdir" \
				"$dataset"
			verify_snapdir_visible $dataset $value ||
				log_fail "$dataset/.zfs is not $value as expected."
		fi
	done
done

log_pass "Setting a valid snapdir property on a dataset succeeds."
