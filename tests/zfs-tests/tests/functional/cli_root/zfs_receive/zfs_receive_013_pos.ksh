#!/bin/ksh -p
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
# Copyright (c) 2015, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/cli_root/cli_common.kshlib

#
# DESCRIPTION:
#   Verifying 'zfs receive' works correctly on deduplicated streams
#
# STRATEGY:
#   1. Create some snapshots with duplicated data
#   2. Send a deduplicated stream of the last snapshot
#   3. Attempt to receive the deduplicated stream
#

src_fs=$TESTPOOL/drecvsrc
temppool=recvtank
dst_fs=$temppool/drecvdest
streamfile=/var/tmp/drecvstream.$$
tpoolfile=/temptank.$$

function cleanup
{
    for fs in $src_fs $dst_fs; do
        datasetexists $fs && log_must zfs destroy -rf $fs
    done
    zpool destroy $temppool
    [[ -f $streamfile ]] && log_must rm -f $streamfile
    [[ -f $tpoolfile ]] && log_must rm -f $tpoolfile
}

log_assert "Verifying 'zfs receive' works correctly on deduplicated streams"
log_onexit cleanup

truncate -s 100M $tpoolfile
log_must zpool create $temppool $tpoolfile
log_must zfs create $src_fs
src_mnt=$(get_prop mountpoint $src_fs) || log_fail "get_prop mountpoint $src_fs"

echo blah > $src_mnt/blah
zfs snapshot $src_fs@base

echo grumble > $src_mnt/grumble
echo blah > $src_mnt/blah2
zfs snapshot $src_fs@snap2

echo grumble > $src_mnt/mumble
echo blah > $src_mnt/blah3
zfs snapshot $src_fs@snap3

log_must eval "zfs send -D -R $src_fs@snap3 > $streamfile"
log_must eval "zfs receive -v $dst_fs < $streamfile"

cleanup

log_pass "Verifying 'zfs receive' works correctly on deduplicated streams"
