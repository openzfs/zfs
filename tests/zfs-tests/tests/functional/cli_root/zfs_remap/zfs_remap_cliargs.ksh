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
# Copyright 2018, loli10K <ezomori.nozomu@gmail.com>. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/removal/removal.kshlib

#
# DESCRIPTION:
# 'zfs remap' should only work with supported parameters.
#
# STRATEGY:
# 1. Prepare a pool where a top-level VDEV has been removed
# 2. Verify every supported parameter to 'zfs remap' is accepted
# 3. Verify other unsupported parameters raise an error
#

# The 'zfs remap' command has been disabled and may be removed.
export ZFS_REMAP_ENABLED=YES

verify_runnable "both"

function cleanup
{
	destroy_pool $TESTPOOL
	rm -f $DISK1 $DISK2
}

log_assert "'zfs remap' should only work with supported parameters"
log_onexit cleanup

f="$TESTPOOL/fs"
v="$TESTPOOL/vol"
s="$TESTPOOL/fs@snap"
b="$TESTPOOL/fs#bmark"
c="$TESTPOOL/clone"

typeset goodparams=("$f" "$v" "$c")
typeset badparams=("-H" "-p" "-?" "$s" "$b" "$f $f" "$f $v" "$f $s")

DISK1="$TEST_BASE_DIR/zfs_remap-1"
DISK2="$TEST_BASE_DIR/zfs_remap-2"

# 1. Prepare a pool where a top-level VDEV has been removed
log_must truncate -s $(($MINVDEVSIZE * 2)) $DISK1
log_must zpool create $TESTPOOL $DISK1
log_must zfs create $f
log_must zfs create -V 1M -s $v
log_must zfs snap $s
log_must zfs bookmark $s $b
log_must zfs clone $s $c
log_must truncate -s $(($MINVDEVSIZE * 2)) $DISK2
log_must zpool add $TESTPOOL $DISK2
log_must zpool remove $TESTPOOL $DISK1
log_must wait_for_removal $TESTPOOL

# 2. Verify every supported parameter to 'zfs remap' is accepted
for param in "${goodparams[@]}"
do
	log_must zfs remap $param
done

# 3. Verify other unsupported parameters raise an error
for param in "${badparams[@]}"
do
	log_mustnot zfs remap $param
done

log_pass "'zfs remap' only works with supported parameters"
