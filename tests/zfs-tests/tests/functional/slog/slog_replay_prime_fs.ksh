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
# Copyright (c) 2026, Amazon.com, Inc. or its affiliates.
#

. $STF_SUITE/tests/functional/slog/slog.kshlib

#
# DESCRIPTION:
#	Verify ZIL replay with ARC priming produces the same file system as
#	replay without it, for every record type priming acts on.
#
# STRATEGY:
#	1. Create files before freezing the pool, so that replay must read
#	   their dnodes and blocks.
#	2. Freeze the pool and log every record type priming acts on:
#	   TX_WRITE (partial head and tail blocks, whole blocks, single-block
#	   files, past EOF, WR_INDIRECT, to an object removed later),
#	   TX_CREATE, TX_MKDIR, TX_SYMLINK, TX_MKXATTR, TX_REMOVE, TX_RMDIR,
#	   TX_LINK, TX_RENAME (including over an existing name),
#	   TX_TRUNCATE, TX_CLONE_RANGE, TX_SETATTR and TX_SETSAXATTR, and on
#	   Linux TX_RENAME_EXCHANGE and TX_RENAME_WHITEOUT, and on FreeBSD
#	   TX_ACL.  Spread the namespace records over several directories.
#	3. Copy the file system and export the pool.  Save the vdevs.
#	4. Import with priming enabled; verify priming ran and the file
#	   system matches the copy.
#	5. Restore the saved vdevs, import with priming disabled, and verify
#	   the result matches both the copy and the primed replay.
#

verify_runnable "global"

typeset SAVED=$TEST_BASE_DIR/disk-slog.saved
typeset FS=/$TESTPOOL/$TESTFS
typeset -i prime_saved=0
typeset -i bclone_saved=0

function cleanup_prime_fs
{
	(( prime_saved != 0 )) && restore_tunable ZIL_REPLAY_PRIME_ARC
	(( bclone_saved != 0 )) && restore_tunable BCLONE_ENABLED
	rm -rf $SAVED
	cleanup
}

function save_vdevs
{
	log_must rm -rf $SAVED
	log_must mkdir -p $SAVED
	log_must cp $VDEV $SDEV $SAVED/
}

function restore_vdevs
{
	typeset f

	for f in $VDEV $SDEV; do
		log_must cp $SAVED/$(basename $f) $f
	done
}

log_assert "ZIL replay with ARC priming matches replay without it"
log_onexit cleanup_prime_fs

log_must setup
log_must save_tunable ZIL_REPLAY_PRIME_ARC
prime_saved=1
log_must save_tunable BCLONE_ENABLED
bclone_saved=1
log_must set_tunable32 BCLONE_ENABLED 1

log_must zpool create $TESTPOOL $VDEV log $SDEV
log_must zfs create -o recordsize=128k -o compression=off -o xattr=dir \
    $TESTPOOL/$TESTFS

#
# 1. Create the objects that replay will modify.
#
typeset -i i
for i in $(seq 1 64); do
	log_must dd if=/dev/urandom of=$FS/f.$i bs=128k count=8 status=none
	log_must touch $FS/l.$i $FS/s.$i
done
log_must dd if=/dev/urandom of=$FS/small bs=1k count=3 status=none
log_must dd if=/dev/urandom of=$FS/doomed bs=128k count=4 status=none
log_must touch $FS/doomed_attr

typeset -i j
for j in $(seq 1 8); do
	log_must mkdir $FS/n.$j $FS/n.$j/olddir
	for i in $(seq 1 8); do
		log_must touch $FS/n.$j/old.$i $FS/n.$j/ren.$i
	done
done
for i in $(seq 1 4); do
	log_must dd if=/dev/urandom of=$FS/trunc.$i bs=128k count=8 status=none
	log_must dd if=/dev/urandom of=$FS/src.$i bs=128k count=8 status=none
	log_must dd if=/dev/urandom of=$FS/dst.$i bs=128k count=8 status=none
	log_must touch $FS/x.$i $FS/y.$i $FS/acl.$i $FS/over.$i
done

sync_all_pools

# Write the ZIL header so records are kept after the freeze.
log_must dd if=/dev/zero of=$FS/sync conv=fdatasync,fsync bs=1 count=1

#
# 2. Freeze the pool and log the records.
#
log_must zpool freeze $TESTPOOL

# TX_WRITE: 100000-400000, a partial head block, whole and partial tail.
for i in $(seq 1 16); do
	log_must dd if=/dev/urandom of=$FS/f.$i bs=100000 count=3 seek=1 \
	    conv=notrunc oflag=sync status=none
done

# TX_WRITE: a partial write within a single block.
for i in $(seq 17 32); do
	log_must dd if=/dev/urandom of=$FS/f.$i bs=1000 count=1 seek=5 \
	    conv=notrunc oflag=sync status=none
done

# TX_WRITE: aligned whole-block overwrites, which need no data read.
for i in $(seq 33 40); do
	log_must dd if=/dev/urandom of=$FS/f.$i bs=128k count=2 seek=3 \
	    conv=notrunc oflag=sync status=none
done

# TX_WRITE: past the end of the file.
for i in $(seq 41 48); do
	log_must dd if=/dev/urandom of=$FS/f.$i bs=4k count=3 seek=300 \
	    conv=notrunc oflag=sync status=none
done

# TX_WRITE: into a file of a single block smaller than the recordsize.
log_must dd if=/dev/urandom of=$FS/small bs=512 count=1 seek=1 \
    conv=notrunc oflag=sync status=none

# TX_WRITE (WR_INDIRECT)
log_must zfs set logbias=throughput $TESTPOOL/$TESTFS
for i in $(seq 49 56); do
	log_must dd if=/dev/urandom of=$FS/f.$i bs=64k count=3 seek=1 \
	    conv=notrunc oflag=sync status=none
done
log_must zfs set logbias=latency $TESTPOOL/$TESTFS

# TX_WRITE and TX_SETATTR to objects that are gone by replay time.
log_must dd if=/dev/urandom of=$FS/doomed bs=1000 count=1 seek=7 \
    conv=notrunc oflag=sync status=none
log_must chmod 600 $FS/doomed_attr
log_must rm $FS/doomed $FS/doomed_attr

# TX_LINK
for i in $(seq 1 64); do
	log_must ln $FS/l.$i $FS/l.$i.link
done

# TX_SETATTR
for i in $(seq 1 64); do
	log_must chmod 640 $FS/s.$i
	log_must touch -cm -t 201311271200 $FS/s.$i
done

# TX_CREATE, TX_MKDIR, TX_SYMLINK, TX_REMOVE, TX_RMDIR and TX_RENAME,
# spread over several directories.
for j in $(seq 1 8); do
	for i in $(seq 1 8); do
		log_must touch $FS/n.$j/new.$i
		log_must rm $FS/n.$j/old.$i
		log_must mv $FS/n.$j/ren.$i $FS/n.$((j % 8 + 1))/moved.$j.$i
	done
	log_must mkdir $FS/n.$j/newdir
	log_must ln -s ../f.$j $FS/n.$j/sym
	log_must rmdir $FS/n.$j/olddir
done

# TX_RENAME over an existing name, which replay removes.
for i in $(seq 1 4); do
	log_must mv $FS/x.$i $FS/over.$i
	log_must touch $FS/x.$i
done

# TX_TRUNCATE: punching a hole.  Changing the size, within a block or to
# zero, is logged as TX_SETATTR on FreeBSD and TX_TRUNCATE on Linux.
typeset have_punch=yes
if is_linux; then
	log_must fallocate --punch-hole -o 100000 -l 300000 $FS/trunc.3
elif ! truncate -d -o 100000 -l 300000 $FS/trunc.3; then
	have_punch=no
	log_note "hole punching is not supported"
fi
log_must truncate -s 70000 $FS/trunc.1
log_must truncate -s 0 $FS/trunc.2
log_must truncate -s 524289 $FS/trunc.4

# TX_CLONE_RANGE: into a new file and over part of an existing one.
for i in $(seq 1 4); do
	log_must clonefile -f $FS/src.$i $FS/clone.$i
	log_must clonefile -f $FS/src.$i $FS/dst.$i 131072 262144 262144
done

# TX_MKXATTR (xattr=dir), then TX_SETSAXATTR (xattr=sa).
for i in $(seq 1 4); do
	log_must set_xattr dirattr HelloWorld $FS/y.$i
done
log_must zfs set xattr=sa $TESTPOOL/$TESTFS
for i in $(seq 1 4); do
	log_must set_xattr saattr HelloWorld $FS/x.$i
done

# TX_RENAME_EXCHANGE and TX_RENAME_WHITEOUT
typeset have_renameat2=no
if is_linux && renameat2 -C; then
	have_renameat2=yes
	for i in $(seq 1 4); do
		log_must renameat2 -x $FS/n.$i/new.1 $FS/n.$((i + 4))/new.1
		log_must renameat2 -w $FS/n.$i/new.2 $FS/n.$i/whiteout
	done
fi

# TX_ACL
typeset have_acl=no
if is_freebsd; then
	have_acl=yes
	for i in $(seq 1 4); do
		log_must setfacl -m u:root:rwx::allow $FS/acl.$i
	done
fi

#
# 3. Copy the file system, export the pool and save the vdevs.
#
log_must mkdir -p $TESTDIR
log_must rsync -aHAX $FS/ $TESTDIR/copy
log_must zfs unmount $FS
log_must eval "zdb -iv $TESTPOOL/$TESTFS > $TESTDIR/zil.txt"
typeset tx
for tx in TX_WRITE TX_CREATE TX_MKDIR TX_SYMLINK TX_MKXATTR TX_REMOVE \
    TX_RMDIR TX_LINK TX_RENAME TX_CLONE_RANGE TX_SETATTR TX_SETSAXATTR; do
	log_must grep -q "$tx " $TESTDIR/zil.txt
done
[[ $have_punch == yes ]] && log_must grep -q "TX_TRUNCATE " $TESTDIR/zil.txt
if [[ $have_renameat2 == yes ]]; then
	log_must grep -q TX_RENAME_EXCHANGE $TESTDIR/zil.txt
	log_must grep -q TX_RENAME_WHITEOUT $TESTDIR/zil.txt
fi
[[ $have_acl == yes ]] && log_must grep -q TX_ACL $TESTDIR/zil.txt
log_must zpool export $TESTPOOL
save_vdevs

#
# 4. Replay with priming.
#
log_must set_tunable32 ZIL_REPLAY_PRIME_ARC 1
typeset -i before=$(prime_arc_count)
log_must zpool import -f -d $VDIR $TESTPOOL
typeset -i after=$(prime_arc_count)
(( after > before )) || log_fail "ARC priming did not run during replay"

log_note "Verify primed replay against the copy"
log_must replay_directory_diff $TESTDIR/copy $FS
log_must zdb -bcv $TESTPOOL
log_must rsync -aHAX $FS/ $TESTDIR/primed
log_must zpool export $TESTPOOL

#
# 5. Replay the same log without priming.
#
restore_vdevs
log_must set_tunable32 ZIL_REPLAY_PRIME_ARC 0
log_must zpool import -f -d $VDIR $TESTPOOL

log_note "Verify unprimed replay against the copy and the primed replay"
log_must replay_directory_diff $TESTDIR/copy $FS
log_must replay_directory_diff $TESTDIR/primed $FS

log_pass "ZIL replay with ARC priming matches replay without it"
