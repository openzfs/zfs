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
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2014, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/cli_root/zfs_set/zfs_set_common.kshlib

#
# DESCRIPTION:
# Setting readonly on a dataset, it should keep the dataset as readonly.
#
# STRATEGY:
# 1. Create pool, then create filesystem and volume within it.
# 2. Setting readonly to each dataset.
# 3. Check the return value and make sure it is 0.
# 4. Verify the stuff under mountpoint is readonly.
#

verify_runnable "both"

function cleanup
{
	for dataset in $TESTPOOL/$TESTFS $TESTPOOL/$TESTVOL ; do
		snapexists ${dataset}@$TESTSNAP && \
			destroy_dataset ${dataset}@$TESTSNAP -R
	done
}

function initial_dataset # $1 dataset
{
	typeset dataset=$1

	typeset fstype=$(get_prop type $dataset)

	if [[ $fstype == "filesystem" ]] ; then
		typeset mtpt=$(get_prop mountpoint $dataset)
		log_must touch $mtpt/$TESTFILE0
		log_must mkdir -p $mtpt/$TESTDIR0
	fi
}


function cleanup_dataset # $1 dataset
{
	typeset dataset=$1

	typeset fstype=$(get_prop type $dataset)

	if [[ $fstype == "filesystem" ]] ; then
		typeset mtpt=$(get_prop mountpoint $dataset)
		log_must rm -f $mtpt/$TESTFILE0
		log_must rm -rf $mtpt/$TESTDIR0
	fi
}

function verify_readonly # $1 dataset, $2 on|off
{
	typeset dataset=$1
	typeset value=$2

	if datasetnonexists $dataset ; then
		log_note "$dataset does not exist!"
		return 1
	fi

	typeset fstype=$(get_prop type $dataset)

	expect="log_must"

	if [[ $2 == "on" ]] ; then
		expect="log_mustnot"
	fi

	case $fstype in
		filesystem)
			typeset mtpt=$(get_prop mountpoint $dataset)
			$expect touch $mtpt/$TESTFILE1
			$expect mkdir -p $mtpt/$TESTDIR1
			$expect eval "echo 'y' | rm $mtpt/$TESTFILE0"
			$expect rmdir $mtpt/$TESTDIR0

			if [[ $expect == "log_must" ]] ; then
				log_must eval "echo 'y' | rm $mtpt/$TESTFILE1"
				log_must rmdir $mtpt/$TESTDIR1
				log_must touch $mtpt/$TESTFILE0
				log_must mkdir -p $mtpt/$TESTDIR0
			fi
			;;
		volume)
			$expect eval "new_fs \
			    ${ZVOL_DEVDIR}/$dataset > /dev/null 2>&1"
			;;
		*)
			;;
	esac

	return 0
}

log_onexit cleanup

log_assert "Setting a valid readonly property on a dataset succeeds."

typeset all_datasets

log_must zfs mount -a

log_must zfs snapshot $TESTPOOL/$TESTFS@$TESTSNAP
log_must zfs clone $TESTPOOL/$TESTFS@$TESTSNAP $TESTPOOL/$TESTCLONE

if is_global_zone ; then
	log_must zfs snapshot $TESTPOOL/$TESTVOL@$TESTSNAP
	log_must zfs clone $TESTPOOL/$TESTVOL@$TESTSNAP $TESTPOOL/$TESTCLONE1
	all_datasets="$TESTPOOL $TESTPOOL/$TESTFS $TESTPOOL/$TESTVOL "
	all_datasets+="$TESTPOOL/$TESTCLONE $TESTPOOL/$TESTCLONE1"
else
	all_datasets="$TESTPOOL $TESTPOOL/$TESTFS $TESTPOOL/$TESTCLONE"
fi


for dataset in $all_datasets; do
	for value in on off; do
		set_n_check_prop "off" "readonly" "$dataset"
		initial_dataset $dataset

		set_n_check_prop "$value" "readonly" "$dataset"
		verify_readonly $dataset $value

		set_n_check_prop "off" "readonly" "$dataset"
		cleanup_dataset $dataset
	done
done

log_pass "Setting a valid readonly property on a dataset succeeds."
