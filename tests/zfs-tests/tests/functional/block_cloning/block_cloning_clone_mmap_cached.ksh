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

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/block_cloning/block_cloning.kshlib

#
# DESCRIPTION:
#	When the destination file is mmaped and is already cached we need to
#	update mmaped pages after successful clone.
#
# STRATEGY:
#	1. Create a pool.
#	2. Create a two test files with random content.
#	3. mmap the files, read them and clone from one to the other using
#	   clone_mmap_cached.
#	4. clone_mmap_cached also verifies if the content of the destination
#	   file was updated while reading it from mmaped memory.
#

verify_runnable "global"

VDIR=$TEST_BASE_DIR/disk-bclone
VDEV="$VDIR/a"

function cleanup
{
	datasetexists $TESTPOOL && destroy_pool $TESTPOOL
	rm -rf $VDIR
}

log_onexit cleanup

log_assert "Test for clone into mmaped and cached file"

log_must rm -rf $VDIR
log_must mkdir -p $VDIR
log_must truncate -s 1G $VDEV

log_must zpool create -o feature@block_cloning=enabled $TESTPOOL $VDEV
log_must zfs create $TESTPOOL/$TESTFS

for opts in "--" "-i" "-o" "-io"
do
	log_must dd if=/dev/urandom of=/$TESTPOOL/$TESTFS/src bs=1M count=1
	log_must dd if=/dev/urandom of=/$TESTPOOL/$TESTFS/dst bs=1M count=1

	# Clear cache.
	log_must zpool export $TESTPOOL
	log_must zpool import -d $VDIR $TESTPOOL

	log_must clone_mmap_cached $opts /$TESTPOOL/$TESTFS/src /$TESTPOOL/$TESTFS/dst

	sync_pool $TESTPOOL
	log_must sync

	log_must have_same_content /$TESTPOOL/$TESTFS/src /$TESTPOOL/$TESTFS/dst
	blocks=$(get_same_blocks $TESTPOOL/$TESTFS src $TESTPOOL/$TESTFS dst)
	# FreeBSD's seq(1) leaves a trailing space, remove it with sed(1).
	log_must [ "$blocks" = "$(seq -s " " 0 7 | sed 's/ $//')" ]
done

log_pass "Clone properly updates mmapped and cached pages"
