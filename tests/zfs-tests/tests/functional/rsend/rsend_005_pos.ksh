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
#	zfs send -R -I send all the incremental between fs@init with fs@final
#
# STRATEGY:
#	1. Setup test model
#	2. Send -R -I @init @final on pool
#	3. Destroy all the snapshots which is later than @init
#	4. Verify receive can restore all the snapshots and data
#	5. Do the same test on filesystem and volume
#

verify_runnable "both"

log_assert "zfs send -R -I send all the incremental between @init with @final"
log_onexit cleanup_pool $POOL2

#
# Duplicate POOL2 for testing
#
log_must eval "zfs send -R $POOL@final > $BACKDIR/pool-final-R"
log_must eval "zfs receive -d -F $POOL2 < $BACKDIR/pool-final-R"

if is_global_zone ; then
	#
	# Testing send -R -I from pool
	#
	log_must eval "zfs send -R -I @init $POOL2@final > " \
		"$BACKDIR/pool-init-final-IR"
	list=$(getds_with_suffix $POOL2 @snapA)
	list="$list $(getds_with_suffix $POOL2 @snapB)"
	list="$list $(getds_with_suffix $POOL2 @snapC)"
	list="$list $(getds_with_suffix $POOL2 @final)"
	log_must destroy_tree $list
	log_must eval "zfs receive -d -F $POOL2 < $BACKDIR/pool-init-final-IR"
	log_must cmp_ds_cont $POOL $POOL2
fi

dstds=$(get_dst_ds $POOL $POOL2)
#
# Testing send -R -I from filesystem
#
log_must eval "zfs send -R -I @init $dstds/$FS@final > " \
	"$BACKDIR/fs-init-final-IR"
list=$(getds_with_suffix $dstds/$FS @snapA)
list="$list $(getds_with_suffix $dstds/$FS @snapB)"
list="$list $(getds_with_suffix $dstds/$FS @snapC)"
list="$list $(getds_with_suffix $dstds/$FS @final)"
log_must destroy_tree $list
if is_global_zone ; then
	log_must eval "zfs receive -d -F $dstds < $BACKDIR/fs-init-final-IR"
else
	zfs receive -d -F $dstds < $BACKDIR/fs-init-final-IR
fi
log_must cmp_ds_subs $POOL $dstds
log_must cmp_ds_cont $POOL $dstds

if is_global_zone ; then
	#
	# Testing send -I -R for volume
	#
	vol=$POOL2/$FS/vol
	log_must eval "zfs send -R -I @init $vol@final > " \
		"$BACKDIR/vol-init-final-IR"
	log_must destroy_tree $vol@snapB $vol@snapC $vol@final
	log_must eval "zfs receive -d -F $POOL2 < $BACKDIR/vol-init-final-IR"
	log_must cmp_ds_subs $POOL $POOL2
	log_must cmp_ds_cont $POOL $POOL2
fi

log_pass "zfs send -R -I send all the incremental between @init with @final"
