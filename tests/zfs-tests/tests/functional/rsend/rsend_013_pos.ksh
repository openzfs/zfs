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
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/rsend/rsend.kshlib

#
# DESCRIPTION:
#	zfs receive -dF with incremental stream will destroy all the
#	dataset that not exist on the sender side.
#
# STRATEGY:
#	1. Setup test model
#	2. Send -R @final on pool
#	3. Destroy some dataset within the @final, and create @destroy
#	4. Send -R -I @final @destroy on pool
#	5. Verify receive -dF will destroy all the dataset that not exist
#	   on the sender side.
#

verify_runnable "both"

function cleanup
{
	cleanup_pool $POOL2
	cleanup_pool $POOL
	log_must setup_test_model $POOL
}

log_assert "zfs receive -dF will destroy all the dataset that not exist" \
	"on the sender side"
log_onexit cleanup

cleanup

#
# Duplicate POOL2 for testing
#
log_must eval "zfs send -R $POOL@final > $BACKDIR/pool-final-R"
log_must eval "zfs receive -dF $POOL2 < $BACKDIR/pool-final-R"

log_must_busy zfs destroy -Rf $POOL/$FS
log_must_busy zfs destroy -Rf $POOL/pclone

if is_global_zone ; then
	log_must_busy zfs destroy -Rf $POOL/vol
fi
log_must zfs snapshot -r $POOL@destroy

log_must eval "zfs send -R -I @final $POOL@destroy > " \
	"$BACKDIR/pool-final-destroy-IR"
log_must eval "zfs receive -dF $POOL2 < $BACKDIR/pool-final-destroy-IR"

dstds=$(get_dst_ds $POOL $POOL2)
log_must cmp_ds_subs $POOL $dstds
log_must cmp_ds_cont $POOL $dstds

log_pass "zfs receive -dF will destroy all the dataset that not exist" \
	"on the sender side"
