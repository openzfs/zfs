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
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/rsend/rsend.kshlib

#
# DESCRIPTION:
#	zfs send -R -i send incremental from fs@init to fs@final.
#
# STRATEGY:
#	1. Create a set of snapshots and fill with data.
#	2. Create sub filesystems.
#	3. Create final snapshot
#	4. Verify zfs send -R -i will backup all the datasets which has
#	   snapshot suffix @final
#

verify_runnable "both"

log_assert "zfs send -R -i send incremental from fs@init to fs@final."
log_onexit cleanup_pool $POOL2

#
# Duplicate POOL2 for testing
#
log_must eval "zfs send -R $POOL@final > $BACKDIR/pool-final-R"
log_must eval "zfs receive -d -F $POOL2 < $BACKDIR/pool-final-R"

if is_global_zone ; then
	#
	# Testing send -R -i backup from pool
	#
	srclist=$(getds_with_suffix $POOL2 @final)
	interlist="$srclist $(getds_with_suffix $POOL2 @snapC)"
	interlist="$interlist $(getds_with_suffix $POOL2 @snapB)"
	interlist="$interlist $(getds_with_suffix $POOL2 @snapA)"

	log_must eval "zfs send -R -i @init $POOL2@final > " \
		"$BACKDIR/pool-init-final-iR"
	log_must destroy_tree $interlist
	log_must eval "zfs receive -d -F $POOL2 < $BACKDIR/pool-init-final-iR"

	# Get current datasets with suffix @final
	dstlist=$(getds_with_suffix $POOL2 @final)
	if [[ $srclist != $dstlist ]]; then
		log_fail "Unexpected: srclist($srclist) != dstlist($dstlist)"
	fi
	log_must cmp_ds_cont $POOL $POOL2
fi

dstds=$(get_dst_ds $POOL $POOL2)
#
# Testing send -R -i backup from filesystem
#
log_must eval "zfs send -R -i @init $dstds/$FS@final > " \
	"$BACKDIR/fs-init-final-iR"

srclist=$(getds_with_suffix $dstds/$FS @final)
interlist="$srclist $(getds_with_suffix $dstds/$FS @snapC)"
interlist="$interlist $(getds_with_suffix $dstds/$FS @snapB)"
interlist="$interlist $(getds_with_suffix $dstds/$FS @snapA)"
log_must destroy_tree $interlist
if is_global_zone ; then
	log_must eval "zfs receive -d -F $POOL2 < $BACKDIR/fs-init-final-iR"
else
	zfs receive -F -d $dstds/$FS < $BACKDIR/fs-init-final-iR
fi

dstlist=$(getds_with_suffix $dstds/$FS @final)
if [[ $srclist != $dstlist ]]; then
	log_fail "Unexpected: srclist($srclist) != dstlist($dstlist)"
fi
log_must cmp_ds_cont $POOL $POOL2

if is_global_zone ; then
	#
	# Testing send -R -i backup from volume
	#
	srclist=$(getds_with_suffix $POOL2/$FS/vol @final)
	log_must eval "zfs send -R -i @init $POOL2/$FS/vol@final > " \
		"$BACKDIR/vol-init-final-iR"
	log_must destroy_tree $srclist
	log_must eval "zfs receive -d $POOL2 < $BACKDIR/vol-init-final-iR"

	dstlist=$(getds_with_suffix $POOL2/$FS/vol @final)
	if [[ $srclist != $dstlist ]]; then
		log_fail "Unexpected: srclist($srclist) != dstlist($dstlist)"
	fi
	log_must cmp_ds_cont $POOL $POOL2
fi

log_pass "zfs send -R -i send incremental from fs@init to fs@final."
