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
#	zfs send -I dataset@init to clone@snap can create a clone
#
# STRATEGY:
#	1. Setup test model
#	2. send -I pool@init to clone@snap
#	3. Verify the clone and snapshot can be recovered via receive
#	4. Verify the similar operating in filesystem and volume
#

verify_runnable "both"

log_assert "zfs send -I send all incrementals from dataset@init to clone@snap"
log_onexit cleanup_pool $POOL2

#
# Duplicate POOL2
#
log_must eval "zfs send -R $POOL@final > $BACKDIR/pool-R"
log_must eval "zfs receive -d -F $POOL2 < $BACKDIR/pool-R"

if is_global_zone ; then
	#
	# Verify send -I backup all incrementals from pool
	#
	log_must eval "zfs send -I $POOL2@psnap $POOL2/pclone@final > " \
		"$BACKDIR/pool-clone-I"
	log_must_busy zfs destroy -rf $POOL2/pclone
	log_must eval "zfs receive -d -F $POOL2 < $BACKDIR/pool-clone-I"
	log_must cmp_ds_subs $POOL $POOL2
	log_must cmp_ds_cont $POOL $POOL2
fi

dstds=$(get_dst_ds $POOL $POOL2)

#
# Verify send -I backup all incrementals from filesystem
#
ds=$dstds/$FS/fs1
log_must eval "zfs send -I $ds/fs2@fsnap $ds/fclone@final > " \
	"$BACKDIR/fs-clone-I"
log_must_busy zfs destroy -rf $ds/fclone
log_must eval "zfs receive -F $ds/fclone < $BACKDIR/fs-clone-I"

log_must cmp_ds_subs $POOL $dstds
log_must cmp_ds_cont $POOL $dstds

if is_global_zone ; then
	#
	# Verify send -I backup all incrementals from volume
	#
	ds=$POOL2/$FS
	log_must eval "zfs send -I $ds/vol@vsnap $ds/vclone@final > " \
		"$BACKDIR/vol-clone-I"
	log_must_busy zfs destroy -rf $ds/vclone
	log_must eval "zfs receive -d -F $POOL2 < $BACKDIR/vol-clone-I"
	log_must cmp_ds_subs $POOL $POOL2
	log_must cmp_ds_cont $POOL $POOL2
fi

log_pass "zfs send -I send all incrementals from dataset@init to clone@snap"
