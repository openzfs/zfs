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
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zfs_set/zfs_set_common.kshlib

#
# DESCRIPTION:
# Setting valid canmount to filesystem, it is successful.
# Whatever is set to volume or snapshot, it is failed.
# 'zfs set canmount=on|off <fs>'
#
# STRATEGY:
# 1. Setup a pool and create fs, volume, snapshot clone within it.
# 2. Loop all the valid mountpoint value.
# 3. Check the return value.
#

verify_runnable "both"

set -A dataset_pos \
	"$TESTPOOL/$TESTFS" "$TESTPOOL/$TESTCTR" "$TESTPOOL/$TESTCLONE"

if is_global_zone ; then
	set -A dataset_neg \
		"$TESTPOOL/$TESTVOL" "$TESTPOOL/$TESTFS@$TESTSNAP" \
		"$TESTPOOL/$TESTVOL@$TESTSNAP"  "$TESTPOOL/$TESTCLONE1"
else
	set -A dataset_neg \
		"$TESTPOOL/$TESTFS@$TESTSNAP" "$TESTPOOL/$TESTVOL@$TESTSNAP"
fi


set -A values "on" "off"

function cleanup
{
	snapexists $TESTPOOL/$TESTFS@$TESTSNAP && \
		destroy_dataset $TESTPOOL/$TESTFS@$TESTSNAP -R

	snapexists $TESTPOOL/$TESTVOL@$TESTSNAP && \
		destroy_dataset $TESTPOOL/$TESTVOL@$TESTSNAP -R

	[[ -n $old_ctr_canmount ]] && \
		log_must zfs set canmount=$old_ctr_canmount $TESTPOOL/$TESTCTR
	[[ -n $old_fs_canmount ]] && \
		log_must zfs set canmount=$old_fs_canmount $TESTPOOL/$TESTFS

	zfs unmount -a > /dev/null 2>&1
	log_must zfs mount -a
}

log_assert "Setting a valid property of canmount to file system, it must be successful."
log_onexit cleanup

typeset old_fs_canmount="" old_ctr_canmount=""

old_fs_canmount=$(get_prop canmount $TESTPOOL/$TESTFS)
[[ $? != 0 ]] && \
	log_fail "Get the $TESTPOOL/$TESTFS canmount error."
old_ctr_canmount=$(get_prop canmount $TESTPOOL/$TESTCTR)
[[ $? != 0 ]] && \
	log_fail "Get the $TESTPOOL/$TESTCTR canmount error."

log_must zfs snapshot $TESTPOOL/$TESTFS@$TESTSNAP
log_must zfs snapshot $TESTPOOL/$TESTVOL@$TESTSNAP
log_must zfs clone $TESTPOOL/$TESTFS@$TESTSNAP $TESTPOOL/$TESTCLONE
log_must zfs clone $TESTPOOL/$TESTVOL@$TESTSNAP $TESTPOOL/$TESTCLONE1

for dataset in "${dataset_pos[@]}" ; do
	for value in "${values[@]}" ; do
		set_n_check_prop "$value" "canmount" "$dataset"
		if [[ $value == "off" ]]; then
			log_mustnot ismounted $dataset
			log_mustnot zfs mount $dataset
			log_mustnot ismounted $dataset
		else
			if ! ismounted $dataset ; then
				log_must zfs mount $dataset
			fi
			log_must ismounted $dataset
		fi
	done
done

for dataset in "${dataset_neg[@]}" ; do
	for value in "${values[@]}" ; do
		set_n_check_prop "$value" "canmount" \
			"$dataset" "false"
		log_mustnot ismounted $dataset
	done
done

log_pass "Setting canmount to filesystem pass."
