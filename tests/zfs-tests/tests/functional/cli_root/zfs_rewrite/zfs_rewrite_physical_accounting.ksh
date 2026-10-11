#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source. A copy of the CDDL is also available via the Internet at
# https://opensource.org/license/CDDL-1.0.
#
# Copyright (c) 2026 Dmitry R.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	Physical rewrites must be accounted and freed using allocation birth.
#
# STRATEGY:
#	1. Physically rewrite clone blocks, then destroy via livelist and bptree.
#	2. Physically rewrite between snapshots, remove the files and newer snap.
#	3. Check snapshot space, surviving data, and offline zdb leak accounting.
#	4. Repeat the clone/snapshot shape with an ordinary rewrite as a control.
#

verify_runnable "global"

typeset pool=${TESTPOOL}_rw_accounting
typeset workdir=$TEST_BASE_DIR/rewrite_accounting.$$
typeset mnt=$workdir/mnt
typeset shared=$(get_tunable LIVELIST_MIN_PERCENT_SHARED)
typeset -i data_size=$((8 * 4 * 1024 * 1024))

function cleanup
{
	log_must set_tunable32 LIVELIST_MIN_PERCENT_SHARED $shared
	poolexists $pool && destroy_pool $pool
	rm -rf $workdir
}

function make_pool
{
	poolexists $pool && log_must destroy_pool $pool
	log_must truncate -s 1G $workdir/vdev
	log_must zpool create -f "$@" -O mountpoint=$mnt \
	    -O compression=off -O recordsize=128k $pool $workdir/vdev
	log_must zfs create $pool/fs
	for i in {1..8}; do
		log_must dd if=/dev/urandom of=$mnt/fs/f$i bs=128k count=32
	done
	log_must cp $mnt/fs/f1 $workdir/expected
	sync_pool $pool
}

function check_no_leaks
{
	sync_pool $pool
	log_must zpool wait -t free $pool
	sync_pool $pool
	log_must_busy zpool export $pool
	if ! zdb -e -p $workdir -b $pool > $workdir/zdb.out 2>&1; then
		cat $workdir/zdb.out
		log_fail "zdb found an allocation accounting error"
	fi
	log_must grep 'No leaks' $workdir/zdb.out
	log_must zpool import -d $workdir $pool
}

log_assert "Physical rewrites preserve clone and snapshot space accounting"
log_onexit cleanup
log_must mkdir -p $workdir
log_must set_tunable32 LIVELIST_MIN_PERCENT_SHARED 0

# A rewritten clone block is a new allocation even with an old logical birth.
make_pool
log_must zfs snapshot $pool/fs@s
log_must zfs clone -o mountpoint=$mnt/clone $pool/fs@s $pool/clone
sync_pool $pool
log_must zfs rewrite -P $mnt/clone/f1 $mnt/clone/f2
sync_pool $pool
log_must cmp $workdir/expected $mnt/clone/f1
log_must zfs destroy $pool/clone
check_no_leaks
log_must cmp $workdir/expected $mnt/fs/f1

# Disable livelists to exercise the async bptree traversal independently.
make_pool -o feature@livelist=disabled
log_must zfs snapshot $pool/fs@s
log_must zfs clone -o mountpoint=$mnt/clone $pool/fs@s $pool/clone
sync_pool $pool
log_must zfs rewrite -P -r $mnt/clone
sync_pool $pool
log_must cmp $workdir/expected $mnt/clone/f1
log_must zfs destroy $pool/clone
check_no_leaks
log_must cmp $workdir/expected $mnt/fs/f1

# The newer snapshot owns the physically rewritten allocation, not s1.
make_pool
log_must zfs snapshot $pool/fs@s1
sync_pool $pool
log_must zfs rewrite -P -r $mnt/fs
sync_pool $pool
log_must cmp $workdir/expected $mnt/fs/f1
log_must zfs snapshot $pool/fs@s2
log_must rm $mnt/fs/f*
sync_pool $pool
log_must zfs destroy $pool/fs@s2
check_no_leaks
typeset -i snap_used=$(zfs get -Hp -o value usedbysnapshots $pool/fs)
log_note "s1 space after destroying s2: $snap_used bytes"
log_must test $snap_used -lt $((data_size + 2 * 1024 * 1024))
log_must cmp $workdir/expected $mnt/fs/.zfs/snapshot/s1/f1
log_must zfs destroy $pool/fs@s1
check_no_leaks

# Recursive destruction must also release physically rewritten blocks.
make_pool
log_must zfs snapshot $pool/fs@s1
sync_pool $pool
log_must zfs rewrite -P -r $mnt/fs
sync_pool $pool
log_must zfs destroy -r $pool/fs
check_no_leaks

# Ordinary rewrites must continue to free both clone and snapshot allocations.
make_pool
log_must zfs snapshot $pool/fs@s1
log_must zfs clone -o mountpoint=$mnt/clone $pool/fs@s1 $pool/clone
sync_pool $pool
log_must zfs rewrite -r $mnt/clone
log_must zfs rewrite -r $mnt/fs
sync_pool $pool
log_must zfs snapshot $pool/fs@s2
log_must rm $mnt/fs/f*
sync_pool $pool
log_must zfs destroy $pool/clone
log_must zfs destroy $pool/fs@s2
check_no_leaks

log_pass "Physical and ordinary rewrites free clone and snapshot allocations"
