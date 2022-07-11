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
