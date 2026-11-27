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

#
# DESCRIPTION:
#	When the dirty pages of a shared mapping cannot be written back,
#	fsync(2), fdatasync(2) and msync(2) with MS_SYNC must return -1 and
#	set errno, not return a positive errno (openzfs #19915).
#
# STRATEGY:
#	1. Create a pool on a file vdev, and a file system holding a file.
#	2. Set the refquota of the file system to the space it references,
#	   store into every page of a shared mapping of the file, and verify
#	   that each sync call fails with EDQUOT.
#	3. Remove the refquota and verify that each sync call succeeds.
#	4. Fill the pool, and verify that each sync call fails with ENOSPC.
#	5. Remove the fill, wait for its space to be returned, and verify
#	   that each sync call succeeds.
#

verify_runnable "global"

POOL=$TESTPOOL1
FS=$POOL/fs
VDEV=$TEST_BASE_DIR/mmap_sync_error.vdev
FILE=$TESTDIR1/fs/file

function cleanup
{
	# Removing the file drops the pages that could not be written back
	if poolexists $POOL; then
		datasetexists $FS && log_must zfs set refquota=none $FS
		log_must rm -f $FILE $TESTDIR1/fill $TESTDIR1/fill2
		destroy_pool $POOL
	fi
	log_must rm -f $VDEV
}

log_assert "Syncing mapped pages that cannot be written back returns -1" \
    "and sets errno"

log_onexit cleanup

log_must truncate -s $MINVDEVSIZE $VDEV
log_must zpool create -O mountpoint=$TESTDIR1 -O compression=off \
    -O recordsize=4k -O atime=off $POOL $VDEV
log_must zfs create $FS
log_must dd if=/dev/urandom of=$FILE bs=1M count=8
sync_pool $POOL

# At its refquota, the file system has no room for the new blocks
# that writing back the pages needs.
log_must zfs set refquota="$(get_prop referenced $FS)" $FS
log_must mmap_sync_errno $FILE EDQUOT
log_must zfs set refquota=none $FS
log_must mmap_sync_errno $FILE 0

# Fill the pool, then once more with smaller writes after the frees of
# the first pass have been returned.
log_mustnot dd if=/dev/urandom of=$TESTDIR1/fill bs=1M
sync_pool $POOL
log_mustnot dd if=/dev/urandom of=$TESTDIR1/fill2 bs=128k
sync_pool $POOL
log_must mmap_sync_errno $FILE ENOSPC

# The second pass may have found no room even to create its file.
log_must rm -f $TESTDIR1/fill $TESTDIR1/fill2

# The fill may be freed in the background, after rm returns: wait until
# there is room to write the file back twice over.
typeset -i need=$((2 * $(get_prop referenced $FS)))
typeset -i tries=0
sync_pool $POOL
while (( $(get_prop available $FS) < need )); do
	(( ++tries <= 60 )) || log_fail "no room after removing the fill:" \
	    "$(zfs list -Hp -o name,used,available -r $POOL)"
	sleep 1
	sync_pool $POOL
done
log_must mmap_sync_errno $FILE 0

log_pass "Syncing mapped pages that cannot be written back returns -1" \
    "and sets errno"
