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
# Copyright 2016, loli10K. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zfs_set/zfs_set_common.kshlib

#
# DESCRIPTION:
#	Verify ZFS can receive custom properties on both filesystems and
#	snapshots from full and incremental streams.
#
# STRATEGY:
#	1. Create a filesystem.
#	2. Snapshot the filesystem.
#	3. Set custom properties on both the fs and snapshots.
#	4. Create different send streams with the properties.
#	5. Receive the send streams and verify the properties.
#

verify_runnable "both"

typeset streamfile_full=$TEST_BASE_DIR/streamfile_full.$$
typeset streamfile_incr=$TEST_BASE_DIR/streamfile_incr.$$
orig=$TESTPOOL/$TESTFS1
dest=$TESTPOOL/$TESTFS2
typeset user_prop=$(valid_user_property 8)
typeset value=$(user_property_value 8)

function cleanup
{
	log_must rm $streamfile_full
	log_must rm $streamfile_incr
	log_must zfs destroy -rf $TESTPOOL/$TESTFS1
	log_must zfs destroy -rf $TESTPOOL/$TESTFS2
}

log_assert "ZFS can receive custom properties."
log_onexit cleanup

#	1. Create a filesystem.
log_must zfs create $orig

#	2. Snapshot the filesystem.
log_must zfs snapshot $orig@snap1
log_must zfs snapshot $orig@snap2
log_must zfs snapshot $orig@snap3

#	3. Set custom properties on both the fs and snapshots.
log_must eval "zfs set '$user_prop'='$value' $orig"
log_must eval "zfs set '$user_prop:snap1'='$value:snap1' $orig@snap1"
log_must eval "zfs set '$user_prop:snap2'='$value:snap2' $orig@snap2"
log_must eval "zfs set '$user_prop:snap3'='$value:snap3' $orig@snap3"

#	4. Create different send streams with the properties.
log_must eval "zfs send -p $orig@snap1 > $streamfile_full"
log_must eval "zfs send -p -I $orig@snap1 $orig@snap3 > $streamfile_incr"

#	5. Receive the send streams and verify the properties.
log_must eval "zfs recv $dest < $streamfile_full"
log_must eval "check_user_prop $dest $user_prop '$value'"
log_must eval "check_user_prop $dest@snap1 '$user_prop:snap1' '$value:snap1'"
log_must eval "zfs recv $dest < $streamfile_incr"
log_must eval "check_user_prop $dest@snap2 '$user_prop:snap2' '$value:snap2'"
log_must eval "check_user_prop $dest@snap3 '$user_prop:snap3' '$value:snap3'"

log_pass "ZFS can receive custom properties passed."
