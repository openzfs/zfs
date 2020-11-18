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
# Copyright (c) 2012, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zfs_set/zfs_set_common.kshlib

#
# DESCRIPTION:
# Setting valid canmount to filesystem, it is successful.
# Whatever is set to volume or snapshot, it is failed.
# 'zfs set canmount=noauto <fs>'
#
# STRATEGY:
# 1. Setup a pool and create fs, volume, snapshot clone within it.
# 2. Set canmount=noauto for each dataset and check the return value
#    and check if it still can be mounted by mount -a or shared by
#    share -a
# 3. mount each dataset(except volume) to see if it can be mounted.
# 4. verify that a mounted dataset can be shared by share -a.
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

function cleanup
{
	i=0
	while (( i < ${#dataset_pos[*]} )); do
		ds=${dataset_pos[i]}
		if datasetexists $ds; then
			log_must zfs set mountpoint=${old_mnt[i]} $ds
			log_must zfs set canmount=${old_canmount[i]} $ds
		fi
		(( i = i + 1 ))
	done

	ds=$TESTPOOL/$TESTCLONE
	if datasetexists $ds; then
		mntp=$(get_prop mountpoint $ds)
		log_must zfs destroy $ds
		if [[ -d $mntp ]]; then
			rm -fr $mntp
		fi
	fi

	if snapexists $TESTPOOL/$TESTFS@$TESTSNAP ; then
		log_must zfs destroy -R $TESTPOOL/$TESTFS@$TESTSNAP
	fi
	if snapexists $TESTPOOL/$TESTVOL@$TESTSNAP ; then
		log_must zfs destroy -R $TESTPOOL/$TESTVOL@$TESTSNAP
	fi

	zfs unmount -a > /dev/null 2>&1
	log_must zfs mount -a

	if [[ -d $tmpmnt ]]; then
		rm -fr $tmpmnt
	fi
}

log_assert "Setting canmount=noauto to file system, it must be successful."
log_onexit cleanup

set -A old_mnt
set -A old_canmount
set -A old_sharenfs
typeset tmpmnt=/tmpmount$$
typeset ds

log_must zfs snapshot $TESTPOOL/$TESTFS@$TESTSNAP
log_must zfs snapshot $TESTPOOL/$TESTVOL@$TESTSNAP
log_must zfs clone $TESTPOOL/$TESTFS@$TESTSNAP $TESTPOOL/$TESTCLONE
log_must zfs clone $TESTPOOL/$TESTVOL@$TESTSNAP $TESTPOOL/$TESTCLONE1

typeset -i i=0
while (( i < ${#dataset_pos[*]} )); do
	ds=${dataset_pos[i]}
	old_mnt[i]=$(get_prop mountpoint $ds)
	old_canmount[i]=$(get_prop canmount $ds)
	old_sharenfs[i]=$(get_prop sharenfs $ds)
	(( i = i + 1 ))
done

i=0
while (( i < ${#dataset_pos[*]} )) ; do
	dataset=${dataset_pos[i]}
	set_n_check_prop "noauto" "canmount" "$dataset"
	log_must zfs set mountpoint=$tmpmnt $dataset
	log_must zfs set sharenfs=on $dataset
	if  ismounted $dataset; then
		zfs unmount -a > /dev/null 2>&1
		log_must mounted $dataset
		log_must zfs unmount $dataset
		log_must unmounted $dataset
		log_must zfs mount -a
		log_must unmounted $dataset
		log_must zfs share -a
		log_mustnot is_exported $tmpmnt
	else
		log_must zfs mount -a
		log_must unmounted $dataset
		zfs unmount -a > /dev/null 2>&1
		log_must unmounted $dataset
	fi

	log_must zfs mount $dataset
	log_must mounted $dataset
	log_must zfs share -a
	log_must is_exported $tmpmnt

	log_must zfs set sharenfs="${old_sharenfs[i]}" $dataset
	log_must zfs set canmount="${old_canmount[i]}" $dataset
	log_must zfs set mountpoint="${old_mnt[i]}" $dataset
	(( i = i + 1 ))
done

for dataset in "${dataset_neg[@]}" ; do
	set_n_check_prop "noauto" "canmount" "$dataset" "false"
	log_mustnot ismounted $dataset
done

log_pass "Setting canmount=noauto to filesystem pass."
