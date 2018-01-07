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
