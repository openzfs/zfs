#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
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
# Copyright (c) 2021 by Nutanix. All rights reserved.
#

. $STF_SUITE/tests/functional/slog/slog.kshlib

#
# DESCRIPTION:
#	Verify saxattr logging in to ZIL works
#
# STRATEGY:
#	1. Create an empty file system (TESTFS)
#	2. Freeze TESTFS
#	3. Create Xattrs.
#	4. Unmount filesystem
#	   <at this stage TESTFS is empty again and unfrozen, and the
#	   intent log contains a complete set of deltas to replay it>
#	5. Remount TESTFS <which replays the intent log>
#	6. Check xattrs.
#

verify_runnable "global"

function cleanup_testenv
{
	cleanup
	log_must set_tunable32 ZIL_SAXATTR $orig_zil_saxattr
}

log_assert "Verify saxattr logging in to ZIL works"

orig_zil_saxattr=$(get_tunable ZIL_SAXATTR)

log_onexit cleanup_testenv
log_must setup

NFILES=10
function validate_zil_saxattr
{
	saxattrzil=$1
	if [ "$2" == "disabled" ]; then
		zilsaxattr_feature_disabled=1
		zpoolcreateflags="-ofeature@zilsaxattr=disabled"
	else
		zilsaxattr_feature_disabled=0
		zpoolcreateflags=""
	fi

	log_must set_tunable32 ZIL_SAXATTR $saxattrzil

	#
	# 1. Create an empty file system (TESTFS)
	#
	log_must zpool create $zpoolcreateflags $TESTPOOL $VDEV log mirror $LDEV
	log_must zfs set compression=on $TESTPOOL
	log_must zfs create -o xattr=sa $TESTPOOL/$TESTFS
	log_must mkdir -p $TESTDIR

	#
	# This dd command works around an issue where ZIL records aren't created
	# after freezing the pool unless a ZIL header already exists. Create a
	# file synchronously to force ZFS to write one out.
	#
	log_must dd if=/dev/zero of=/$TESTPOOL/$TESTFS/sync \
	    conv=fdatasync,fsync bs=1 count=1

	#
	# 2. Freeze TESTFS
	#
	log_must zpool freeze $TESTPOOL

	rm /$TESTPOOL/$TESTFS/sync
	#
	# 3. Create xattrs
	#
	for i in $(seq $NFILES); do
		log_must mkdir /$TESTPOOL/$TESTFS/xattr.d.$i
		log_must set_xattr test test /$TESTPOOL/$TESTFS/xattr.d.$i

		log_must touch /$TESTPOOL/$TESTFS/xattr.f.$i
		log_must set_xattr test test /$TESTPOOL/$TESTFS/xattr.f.$i
	done

	#
	# 4. Unmount filesystem and export the pool
	#
	# At this stage TESTFS is empty again and unfrozen, and the
	# intent log contains a complete set of deltas to replay it.
	#
	log_must zfs unmount /$TESTPOOL/$TESTFS

	log_note "Verify transactions to replay:"
	log_must zdb -iv $TESTPOOL/$TESTFS

	log_must zpool export $TESTPOOL

	#
	# 5. Remount TESTFS <which replays the intent log>
	#
	# Import the pool to unfreeze it and claim log blocks.  It has to be
	# `zpool import -f` because we can't write a frozen pool's labels!
	#
	log_must zpool import -f -d $VDIR $TESTPOOL

	#
	# 6. Verify Xattr
	# If zilsaxattr_feature_disabled=1 or saxattrzil=0, then xattr=sa
	# logging in ZIL is not enabled, So, xattrs would be lost.
	# If zilsaxattr_feature_disabled=0 and saxattrzil=1, then xattr=sa
	# logging in ZIL is enabled, So, xattrs shouldn't be lost.
	#
	for i in $(seq $NFILES); do
		if [ $zilsaxattr_feature_disabled -eq 1 -o \
		    $saxattrzil -eq 0 ]; then
			log_mustnot get_xattr test /$TESTPOOL/$TESTFS/xattr.d.$i
			log_mustnot get_xattr test /$TESTPOOL/$TESTFS/xattr.f.$i
		else
			log_must get_xattr test /$TESTPOOL/$TESTFS/xattr.d.$i
			log_must get_xattr test /$TESTPOOL/$TESTFS/xattr.f.$i
		fi
	done

	cleanup
	log_must setup
}


#Validate zilsaxattr feature enabled.
validate_zil_saxattr 0
validate_zil_saxattr 1
#Validate zilsaxattr feature disabled.
validate_zil_saxattr 0 disabled
validate_zil_saxattr 1 disabled

log_pass "Verify saxattr logging in to ZIL works"
