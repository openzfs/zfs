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
#	Long name filesystem with snapshot should not break ZFS.
#
# STRATEGY:
#	1. Create filesystem and snapshot.
#	2. When the snapshot length is 256, rename the filesystem.
#	3. Verify it does not break ZFS
#

verify_runnable "both"

function cleanup
{
	datasetexists $initfs && destroy_dataset $initfs -rf
}

log_assert "Verify long name filesystem with snapshot should not break ZFS."
log_onexit cleanup

initfs=$TESTPOOL/$TESTFS/$TESTFS
basefs=$initfs
typeset -i ret=0 len snaplen
while ((ret == 0)); do
	zfs create $basefs
	zfs snapshot $basefs@snap1
	ret=$?

	if ((ret != 0)); then
		len=$(( ${#basefs} + 1 )) # +1 for NUL
		log_note "The deeply-nested filesystem len: $len"

		#
		# Make sure there are at lease 2 characters left
		# for snapshot name space, otherwise snapshot name
		# is incorrect
		#
		if ((len >= 255)); then
			datasetexists $basefs && destroy_dataset $basefs -r
			basefs=${basefs%/*}
			len=$(( ${#basefs} + 1 ))
		fi
		break
	fi

	basefs=$basefs/$TESTFS
done

# Make snapshot name length match the longest one
((snaplen = 256 - len - 1)) # 1: @
snap=$(gen_dataset_name $snaplen "s")
log_must zfs snapshot $basefs@$snap

log_mustnot zfs rename $basefs ${basefs}a
log_mustnot zfs rename $basefs ${basefs}-new
log_mustnot zfs rename $initfs ${initfs}-new
log_mustnot zfs rename $TESTPOOL/$TESTFS $TESTPOOL/$TESTFS-new

log_pass "Verify long name filesystem with snapshot should not break ZFS."
