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

typeset old_fs_canmount=$(get_prop canmount $TESTPOOL/$TESTFS)
typeset old_ctr_canmount=$(get_prop canmount $TESTPOOL/$TESTCTR)

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
