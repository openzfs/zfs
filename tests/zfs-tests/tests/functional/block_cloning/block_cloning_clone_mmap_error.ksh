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
#	When the refresh of a mapped page after a clone fails, the mapping,
#	read() and the file must not show the page's old contents, and a
#	store made through the mapping before the clone must be kept.
#
# STRATEGY:
#	1. Create a pool, and a file system with 1 KiB records.
#	2. Create the files src and dst, then export and import the pool so
#	   that reads of them go to disk.
#	3. With clone_mmap_error, map dst, make its reads fail with zinject,
#	   and clone the first record of src over it.
#	4. clone_mmap_error then checks the mapping, read() and the file.
#	5. Repeat with a store to the mapping before the clone.
#

verify_runnable "global"

VDIR=$TEST_BASE_DIR/disk-bclone
VDEV="$VDIR/a"
FS=/$TESTPOOL/$TESTFS

function cleanup
{
	zinject -c all > /dev/null 2>&1
	datasetexists $TESTPOOL && destroy_pool $TESTPOOL
	rm -rf $VDIR
}

function fill # <file> <char>
{
	dd if=/dev/zero bs=128k count=1 2> /dev/null | tr '\0' "$2" > "$1"
}

log_onexit cleanup

log_assert "A failed refresh of a mapped page after a clone shows no old data"

log_must rm -rf $VDIR
log_must mkdir -p $VDIR
log_must truncate -s 1G $VDEV

log_must zpool create -o feature@block_cloning=enabled $TESTPOOL $VDEV
log_must zfs create -o recordsize=1k -o compression=off $TESTPOOL/$TESTFS

for opt in "--" "-w"
do
	log_must fill $FS/src S
	log_must fill $FS/dst D
	log_must zpool export $TESTPOOL
	log_must zpool import -d $VDIR $TESTPOOL

	log_must clone_mmap_error $opt $FS/src $FS/dst \
	    "zinject -t data -e io $FS/dst" "zinject -c all" \
	    "zpool sync $TESTPOOL"
done

log_pass "A failed refresh of a mapped page after a clone shows no old data"
