#!/bin/ksh -p

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
# Copyright (c) 2015 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/rsend/rsend.kshlib

#
# Description:
# Verify compressed send streams replicate data and datasets
#
# Strategy:
# 1. Back up all the data from POOL/FS
# 2. Verify all the datasets and data can be recovered in POOL2
# 3. Back up all the data from root filesystem POOL2
# 4. Verify all the data can be recovered, too
#

verify_runnable "both"

log_assert "zfs send -c -R send replication stream up to the named snap."
log_onexit cleanup_pool $POOL2

# Verify the entire pool and descendants can be backed up and restored.
log_must eval "zfs send -c -R $POOL@final > $BACKDIR/pool-final-R"
log_must eval "zfs receive -d -F $POOL2 < $BACKDIR/pool-final-R"

dstds=$(get_dst_ds $POOL $POOL2)
log_must cmp_ds_subs $POOL $dstds
log_must cmp_ds_cont $POOL $dstds

# Cleanup POOL2
log_must cleanup_pool $POOL2

# Verify all the filesystems and descendants can be backed up and restored.
log_must eval "zfs send -c -R $POOL/$FS@final > $BACKDIR/fs-final-R"
log_must eval "zfs receive -d $POOL2 < $BACKDIR/fs-final-R"

dstds=$(get_dst_ds $POOL/$FS $POOL2)
log_must cmp_ds_subs $POOL/$FS $dstds
log_must cmp_ds_cont $POOL/$FS $dstds

log_pass "zfs send -c -R send replication stream up to the named snap."
