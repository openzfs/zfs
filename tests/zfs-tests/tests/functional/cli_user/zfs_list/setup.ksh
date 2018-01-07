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
