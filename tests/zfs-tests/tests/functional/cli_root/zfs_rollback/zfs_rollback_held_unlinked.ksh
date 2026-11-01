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
#	A rollback must not leave a stale, hashed inode behind for a file
#	that was open-unlinked in the snapshot and is still held open.
#
# STRATEGY:
#	1. Create filler files until allocation leaves the first dnode
#	   chunk, then open several files, unlink them and keep the
#	   descriptors open.
#	2. Snapshot the filesystem; the snapshot's unlinked set now contains
#	   the zero-link objects.
#	3. Roll back to the snapshot while the descriptors are still open.
#	   zfs_rezget() succeeds for these objects (same generation, zero
#	   links) and the unlinked drain then frees them.
#	4. Create files until one reuses a held object number.  Before the
#	   fix the old inode was still hashed and zfs_znode_alloc() failed
#	   VERIFY0(insert_inode_locked(ip)) with EBUSY.
#	5. Verify that the old descriptors cannot modify the new file.
#

verify_runnable "global"

typeset fs=$TESTPOOL/$TESTFS
typeset snap=$fs@held_unlinked
typeset newdir=$TESTDIR/held_unlinked
typeset filldir=$TESTDIR/held_unlinked.fill
typeset -i maxfiles=2000
set -A held

function cleanup
{
	typeset -i fd
	for fd in 5 6 7 8 9; do
		eval "exec $fd<&-"
	done
	datasetexists $snap && destroy_dataset $snap
	rm -rf $newdir $filldir $TESTDIR/held_unlinked.*
}

log_assert "Rollback with held open-unlinked files must not collide inodes"
log_onexit cleanup

#
# After the rollback reopens the objset, new objects are allocated from the
# second dnode chunk (2^dmu_object_alloc_chunk_shift, by default 128 slots)
# onward, and the first chunk is not revisited.  Keep the held objects out
# of it so that a few creates reliably reuse their numbers.
#
typeset -i n=0
log_must mkdir $filldir
while :; do
	((n += 1))
	log_must touch $filldir/f$n
	(($(get_objnum $filldir/f$n) >= 128)) && break
	((n < 1024)) || log_unsupported "objects stay below 128 after $n files"
done

typeset -i fd
for fd in 5 6 7 8 9; do
	typeset file=$TESTDIR/held_unlinked.$fd
	eval "exec $fd<>$file" || log_fail "failed to open $file"
	print -u$fd "held $fd" || log_fail "failed to write $file"
	held[$fd]=$(get_objnum $file)
	log_must rm $file
done
log_note "held open-unlinked objects: ${held[*]}"
log_must zpool sync $TESTPOOL

log_must zfs snapshot $snap
log_must touch $TESTDIR/held_unlinked.post
log_must zfs rollback $snap
log_must zpool sync $TESTPOOL

log_must mkdir $newdir
typeset -i i=0 reused=0
typeset ino
while ((i < maxfiles)); do
	((i += 1))
	log_must eval "print new > $newdir/f$i"
	ino=$(get_objnum $newdir/f$i)
	for fd in 5 6 7 8 9; do
		if [[ $ino == ${held[$fd]} ]]; then
			reused=$fd
			break 2
		fi
	done
	((i % 100 == 0)) && log_must zpool sync $TESTPOOL
done

((reused != 0)) ||
    log_unsupported "no held object number was reused in $maxfiles creates"
log_note "$newdir/f$i reused object $ino, held by fd $reused"

# Use an external writer: a failed write from a ksh builtin leaves the
# shell's own output broken.
log_mustnot eval "dd if=/dev/zero bs=1 count=1 status=none >&$reused"
log_must zpool sync $TESTPOOL
[[ $(<$newdir/f$i) == "new" ]] ||
    log_fail "stale descriptor modified the new file: $(<$newdir/f$i)"

log_pass "Rollback with held open-unlinked files does not collide inodes"
