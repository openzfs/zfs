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
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zfs_get/zfs_get_list_d.kshlib
. $STF_SUITE/tests/functional/cli_user/zfs_list/zfs_list.cfg

DISK=${DISKS%% *}

default_setup_noexit $DISK

# create datasets and set checksum options
set -A cksumarray $CKSUMOPTS
typeset -i index=0
for dataset in $DATASETS
do
	log_must zfs create $TESTPOOL/$TESTFS/$dataset
	sleep 1
        log_must zfs snapshot $TESTPOOL/$TESTFS/${dataset}@snap

	sleep 1
	if is_global_zone ; then
		log_must zfs create -V 64M $TESTPOOL/$TESTFS/${dataset}-vol
		sleep 1
		log_must zfs snapshot $TESTPOOL/$TESTFS/${dataset}-vol@snap
	fi

	# sleep to ensure that the datasets have different creation dates
	sleep 1
	log_must zfs set checksum=${cksumarray[$index]} \
		$TESTPOOL/$TESTFS/$dataset
	if datasetexists $TESTPOOL/$TESTFS/${dataset}-vol; then
		log_must zfs set checksum=${cksumarray[$index]} \
			$TESTPOOL/$TESTFS/${dataset}-vol
	fi

        index=$((index + 1))
done

depth_fs_setup

log_pass
