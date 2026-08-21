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
# http://www.illumos.org/license/CDDL.
#
# CDDL HEADER END
#

# DESCRIPTION:
#	zdb's --class block filter resolves a block's allocation class
#	through the top vdev's metaslab array. An indirect vdev has no
#	metaslabs of its own; zdb synthesizes them only when leak tracking
#	is enabled, so under -L the array is absent and any block whose
#	first DVA names a removed vdev used to dereference it.
#
# STRATEGY:
#	1. Create a pool on a single disk and write a file to it, so every
#	   data block is allocated from that disk.
#	2. Add a second disk and remove the first, leaving an indirect vdev
#	   that every one of those blocks is now reached through.
#	3. Run zdb -bL with a class filter. It must not crash.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/removal/removal.kshlib

verify_runnable "global"

log_assert "zdb -L --class does not fault on an indirect vdev"

set -A disks $DISKS
if [[ ${#disks[@]} -lt 2 ]]; then
	log_unsupported "needs at least two disks"
fi

function cleanup
{
	if poolexists $TESTPOOL ; then
		destroy_pool $TESTPOOL
	fi
}

log_onexit cleanup

# The pool starts as a single vdev, so the file below can only be
# allocated from ${disks[0]} and is guaranteed to be reached through the
# indirect vdev once that disk is removed.
log_must zpool create -f -O compression=off $TESTPOOL ${disks[0]}
log_must zfs create $TESTPOOL/$TESTFS
typeset mountpoint=$(get_prop mountpoint $TESTPOOL/$TESTFS)
log_must dd if=/dev/urandom of=$mountpoint/file1 bs=128k count=8
sync_pool $TESTPOOL

log_must zpool add $TESTPOOL ${disks[1]}
log_must zpool remove $TESTPOOL ${disks[0]}
log_must wait_for_removal $TESTPOOL
log_mustnot vdevs_in_pool $TESTPOOL ${disks[0]}

# -L skips the synthesized metaslabs, and any class filter enters the
# classification path for every traversed block.
log_must zdb -bL --class=normal $TESTPOOL
log_must zdb -bL --class=normal,special,dedup,other $TESTPOOL

log_pass "zdb -L --class does not fault on an indirect vdev"
