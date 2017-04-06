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
