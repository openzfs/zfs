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
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	Verify recursive snapshotting could not break ZFS.
#
# STRATEGY:
#	1. Create deeply-nested filesystems until it is too long to create snap
#	2. Verify zfs snapshot -r pool@snap will not break ZFS
#

verify_runnable "both"

function cleanup
{
	datasetexists $initfs && destroy_dataset $initfs -rf
}

log_assert "Verify recursive snapshotting could not break ZFS."
log_onexit cleanup

initfs=$TESTPOOL/$TESTFS/$TESTFS
basefs=$initfs
typeset -i ret=0 len snaplen
while ((ret == 0)); do
	zfs create $basefs
	zfs snapshot $basefs@snap1
	ret=$?

	if ((ret != 0)); then
		len=${#basefs}
		log_note "The deeply-nested filesystem len: $len"

		#
		# Make sure there are at least 2 characters left
		# for snapshot name space, otherwise snapshot name
		# is incorrect
		#
		if ((len >= 255)); then
			datasetexists $basefs && destroy_dataset $basefs -r
			basefs=${basefs%/*}
			len=${#basefs}
		fi
		break
	fi

	basefs=$basefs/$TESTFS
done

# Make snapshot name is longer than the max length
((snaplen = 256 - len + 10))
snap=$(gen_dataset_name $snaplen "s")
log_mustnot zfs snapshot -r $TESTPOOL@$snap

log_must datasetnonexists $TESTPOOL@$snap
while [[ $basefs != $TESTPOOL ]]; do
	log_must datasetnonexists $basefs@$snap
	basefs=${basefs%/*}
done

log_pass "Verify recursive snapshotting could not break ZFS."
