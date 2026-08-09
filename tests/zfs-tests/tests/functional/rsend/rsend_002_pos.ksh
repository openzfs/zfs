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
#	zfs send -I sends all incrementals from fs@init to fs@final.
#
# STRATEGY:
#	1. Create several snapshots in pool2
#	2. Send -I @snapA @final
#	3. Destroy all the snapshot except @snapA
#	4. Make sure all the snapshots and content are recovered
#

verify_runnable "both"

log_assert "zfs send -I sends all incrementals from fs@init to fs@final."
log_onexit cleanup_pool $POOL2

#
# Duplicate POOL2
#
log_must eval "zfs send -R $POOL@final > $BACKDIR/pool-R"
log_must eval "zfs receive -d -F $POOL2 < $BACKDIR/pool-R"

if is_global_zone ; then
	#
	# Verify send -I will backup all the incrementals in pool
	#
	log_must eval "zfs send -I $POOL2@init $POOL2@final > " \
		"$BACKDIR/pool-init-final-I"
	log_must destroy_tree $POOL2@final $POOL2@snapC $POOL2@snapA
	log_must eval "zfs receive -d -F $POOL2 < $BACKDIR/pool-init-final-I"
	log_must cmp_ds_subs $POOL $POOL2
	log_must cmp_ds_cont $POOL $POOL2
fi

dstds=$(get_dst_ds $POOL $POOL2)

#
# Verify send -I will backup all the incrementals in filesystem
#
log_must eval "zfs send -I @init $dstds/$FS@final > $BACKDIR/fs-init-final-I"
log_must destroy_tree $dstds/$FS@final $dstds/$FS@snapC $dstds/$FS@snapB
log_must eval "zfs receive -d -F $dstds < $BACKDIR/fs-init-final-I"
log_must cmp_ds_subs $POOL $dstds
log_must cmp_ds_cont $POOL $dstds

if is_global_zone ; then
	#
	# Verify send -I will backup all the incrementals in volume
	#
	dataset=$POOL2/$FS/vol
	log_must eval "zfs send -I @vsnap $dataset@final > " \
		"$BACKDIR/vol-vsnap-final-I"
	log_must destroy_tree $dataset@final $dataset@snapC  \
		$dataset@snapB $dataset@init
	log_must eval "zfs receive -d -F $POOL2 < $BACKDIR/vol-vsnap-final-I"
	log_must cmp_ds_subs $POOL $POOL2
	log_must cmp_ds_cont $POOL $POOL2
fi

log_pass "zfs send -I sends all incrementals from fs@init to fs@final."
