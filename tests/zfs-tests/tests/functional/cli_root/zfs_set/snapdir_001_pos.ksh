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
