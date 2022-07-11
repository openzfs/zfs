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
