#!/bin/ksh

#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# http://www.illumos.org/license/CDDL.
#

#
# Copyright (c) 2014, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/rsend/rsend.kshlib

#
# Description:
# Verify that a pool imported readonly can be sent and received.
#
# Strategy:
# 1. Make the source pool readonly, and receive it into pool2.
# 2. Reset pool2, and repeat the send from a non-root fs of the source pool.
# 3. Make the source pool read-write again.
#

verify_runnable "both"

log_assert "zfs send will work on filesystems and volumes in a read-only pool."
log_onexit cleanup_pool $POOL2

log_must zpool export $POOL
log_must zpool import -o readonly=on $POOL
log_must eval "zfs send -R $POOL@final > $BACKDIR/pool-final-R"
log_must eval "zfs receive -d -F $POOL2 < $BACKDIR/pool-final-R"

dstds=$(get_dst_ds $POOL $POOL2)
log_must cmp_ds_subs $POOL $dstds
log_must cmp_ds_cont $POOL $dstds

log_must cleanup_pool $POOL2

log_must eval "zfs send -R $POOL/$FS@final > $BACKDIR/fs-final-R"
log_must eval "zfs receive -d $POOL2 < $BACKDIR/fs-final-R"
log_must_busy zpool export $POOL
log_must zpool import $POOL

dstds=$(get_dst_ds $POOL/$FS $POOL2)
log_must cmp_ds_subs $POOL/$FS $dstds
log_must cmp_ds_cont $POOL/$FS $dstds

log_pass "zfs send will work on filesystems and volumes in a read-only pool."
