#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# CDDL HEADER START
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
# CDDL HEADER END
#

#
# Copyright (c) 2020 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# ZFS receive test to handle Issue #10698
#
# STRATEGY:
# 1. Create a pool with filesystem_limits disabled
# 2. Create a filesystem on that pool
# 3. Enable filesystem limits on that pool
# 4. On a pool with filesystem limits enabled, create a filesystem and set a
#    limit
# 5. Snapshot limited filesystem
# 6. send -R limited filesystem and receive over filesystem with limits disabled
#

verify_runnable "both"

function cleanup
{
	destroy_pool "$poolname"
	destroy_pool "$rpoolname"
	log_must rm -f "$vdevfile"
	log_must rm -f "$rvdevfile"
	log_must rm -f "$streamfile"
}

log_onexit cleanup

log_assert "ZFS should handle receiving streams with filesystem limits on \
	pools where the feature was recently enabled"

poolname=sendpool
rpoolname=recvpool
vdevfile="$TEST_BASE_DIR/vdevfile.$$"
rvdevfile="$TEST_BASE_DIR/rvdevfile.$$"
sendfs="$poolname/fs"
recvfs="$rpoolname/rfs"
streamfile="$TEST_BASE_DIR/streamfile.$$"

log_must truncate -s $MINVDEVSIZE "$rvdevfile"
log_must truncate -s $MINVDEVSIZE "$vdevfile"
log_must zpool create -O mountpoint=none -o feature@filesystem_limits=disabled \
	 "$rpoolname" "$rvdevfile"
log_must zpool create -O mountpoint=none "$poolname" "$vdevfile"

log_must zfs create "$recvfs"
log_must zpool set feature@filesystem_limits=enabled "$rpoolname"

log_must zfs create -o filesystem_limit=100 "$sendfs"
log_must zfs snapshot "$sendfs@a"

log_must eval "zfs send -R \"$sendfs@a\" >\"$streamfile\""
log_must eval "zfs recv -svuF \"$recvfs\" <\"$streamfile\""

log_pass "ZFS can handle receiving streams with filesystem limits on \
	pools where the feature was recently enabled"
