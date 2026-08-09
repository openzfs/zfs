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
#	ZFS can handle stream with multiple identical (same GUID) snapshots
#
# STRATEGY:
#	1. Recursively backup snapshot
#	2. Restore it to the given filesystem
#	3. Resend the snapshot again
#	4. Verify this stream can be restore to this filesystem again
#

verify_runnable "both"

log_assert "ZFS can handle stream with multiple identical (same GUID) snapshots"
log_onexit cleanup_pool $POOL2

log_must zfs create $POOL2/$FS
log_must zfs snapshot $POOL2/$FS@snap

#
# First round restore the stream
#
log_must eval "zfs send -R $POOL2/$FS@snap > $BACKDIR/fs-R"
log_must eval "zfs receive -d -F $POOL2/$FS < $BACKDIR/fs-R"

#
# In order to avoid 'zfs send -R' failed, create snapshot for
# all the sub-systems
#
list=$(zfs list -r -H -o name -t filesystem $POOL2/$FS)
for item in $list ; do
	if datasetnonexists $item@snap ; then
		log_must zfs snapshot $item@snap
	fi
done

#
# Second round restore the stream
#
log_must eval "zfs send -R $POOL2/$FS@snap > $BACKDIR/fs-R"
dstds=$(get_dst_ds $POOL2/$FS $POOL2/$FS)
log_must eval "zfs receive -d -F $dstds < $BACKDIR/fs-R"

log_pass "ZFS can handle stream with multiple identical (same GUID) snapshots"
