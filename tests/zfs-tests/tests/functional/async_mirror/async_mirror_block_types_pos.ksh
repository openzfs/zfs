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
# Copyright (c) 2026, John White.
#

. $STF_SUITE/tests/functional/async_mirror/async_mirror.kshlib

#
# DESCRIPTION:
#	The copy reaches each kind of block that a txg allocates, and finds
#	no false errors: compressed, dedup, copies=3, encrypted with no
#	loaded key, a volume, a clone, a sparse file, a snapshot, blocks
#	with no checksum and gang blocks.  It takes the bytes from the ARC
#	when the ARC holds them, and reads the leader when it does not.
#
# STRATEGY:
#	1. Create a pool with a two-way mirror, turn the async mirror on, and create
#	   datasets with compression, dedup with sha256, copies=3, no
#	   checksum and encryption, a file system for gang blocks, and a
#	   sparse volume.
#	2. With the copy stopped, write to each of them, clone a file, write
#	   a sparse file, and take a snapshot.  Unload the key of the
#	   encrypted dataset.  Restart the copy and wait for it.  Verify no
#	   failure and no data error, and that bytes were copied.
#	3. Export.  Import, force gang blocks, and with the copy stopped
#	   write the same workload and a file of 128 KiB blocks with and
#	   without a checksum.  Export, so that the ARC does not hold the
#	   new blocks, and verify with zdb that the two files have gang
#	   blocks.  Restart the copy and import.  Verify no failure and no
#	   data error.
#	4. Import the lagging child alone, scrub it, and verify that it has
#	   no errors and that the files are intact.
#

verify_runnable "global"

typeset keyfile=$TEST_BASE_DIR/async_mirror_064.key

typeset -r force=$(get_tunable METASLAB_FORCE_GANGING)
typeset -r force_pct=$(get_tunable METASLAB_FORCE_GANGING_PCT)

function cleanup
{
	async_mirror_set_tunable64 METASLAB_FORCE_GANGING $force
	async_mirror_set_tunable32 METASLAB_FORCE_GANGING_PCT $force_pct
	cleanup_async_mirror
	rm -f $keyfile
}

#
# Succeed if the first data block of an object of the exported test pool is
# a gang block: zdb prints its first DVA with a G at the end.
#
# $1 dataset
# $2 object
#
function has_gang # dataset object
{
	zdb -e -p $ASYNC_MIRROR_DIR -dddddd $1 $2 |
	    awk '$2 == "L0" { print $3; exit }' | grep -q 'G$'
}

log_onexit cleanup

#
# Write files to each dataset, a clone, a sparse file, the volume, and a
# snapshot of all.
#
# $1 prefix of the names
# $2 offset in the volume, in 128 KiB blocks
#
function workload # prefix offset
{
	typeset ds

	for ds in lz4 dedup copies nocksum crypt; do
		async_mirror_write /$TESTPOOL/$ds/$1 8
	done
	log_must cp /$TESTPOOL/dedup/$1 /$TESTPOOL/dedup/$1.dup
	async_mirror_write /$TESTPOOL/$1.src 8
	log_must clonefile -f /$TESTPOOL/$1.src /$TESTPOOL/$1.clone
	log_must truncate -s 64m /$TESTPOOL/$1.sparse
	log_must dd if=/dev/urandom of=/$TESTPOOL/$1.sparse bs=128k count=2 \
	    seek=100 conv=notrunc
	log_must dd if=/dev/urandom of=$ZVOL_DEVDIR/$TESTPOOL/vol bs=128k \
	    count=8 seek=$2 conv=fsync
	log_must zfs snapshot -r $TESTPOOL@$1
	sync_pool $TESTPOOL
}

#
# Print the digests of the files of the workload.
#
function digests # prefix
{
	typeset ds

	for ds in lz4 dedup copies nocksum; do
		xxh128digest /$TESTPOOL/$ds/$1
	done
	xxh128digest /$TESTPOOL/$1.clone
	xxh128digest /$TESTPOOL/$1.sparse
}

log_must eval "echo password > $keyfile"
async_mirror_create_pool
async_mirror_enable
log_must zfs create -o compression=lz4 $TESTPOOL/lz4
log_must zfs create -o dedup=on -o checksum=sha256 $TESTPOOL/dedup
log_must zfs create -o copies=3 $TESTPOOL/copies
log_must zfs create -o checksum=off -o recordsize=128k $TESTPOOL/nocksum
log_must zfs create -o recordsize=128k $TESTPOOL/gang
log_must zfs create -o encryption=on -o keyformat=passphrase \
    -o keylocation=file://$keyfile $TESTPOOL/crypt
log_must zfs create -s -V 16m $TESTPOOL/vol
block_device_wait $ZVOL_DEVDIR/$TESTPOOL/vol
log_must wait_async_mirror_idle

# The ARC holds the new blocks.
async_mirror_suspend_copy 1
workload w1 0
typeset sums1="$(digests w1)"
log_must zfs unmount $TESTPOOL/crypt
log_must zfs unload-key $TESTPOOL/crypt
async_mirror_suspend_copy 0
log_must wait_async_mirror_idle
log_note "copied $(get_async_mirror_stat copied_bytes) bytes"
log_must test "$(get_async_mirror_stat failed)" == 0
log_must test "$(get_async_mirror_stat data_errors)" == 0
log_must test "$(get_async_mirror_stat copied_bytes)" -gt 0
log_must zpool export $TESTPOOL

# Gang blocks, and the ARC does not hold the new blocks.
log_must zpool import -l -d $ASYNC_MIRROR_DIR $TESTPOOL
block_device_wait $ZVOL_DEVDIR/$TESTPOOL/vol
log_must async_mirror_set_tunable64 METASLAB_FORCE_GANGING 100000
log_must async_mirror_set_tunable32 METASLAB_FORCE_GANGING_PCT 100
async_mirror_suspend_copy 1
workload w2 8
for ds in gang nocksum; do
	log_must dd if=/dev/urandom of=/$TESTPOOL/$ds/g bs=128k count=4
done
sync_pool $TESTPOOL
typeset sums2="$(digests w2; xxh128digest /$TESTPOOL/gang/g;
    xxh128digest /$TESTPOOL/nocksum/g)"
typeset gobj=$(ls -i /$TESTPOOL/gang/g | awk '{print $1}')
typeset nobj=$(ls -i /$TESTPOOL/nocksum/g | awk '{print $1}')
log_must async_mirror_set_tunable64 METASLAB_FORCE_GANGING $force
log_must async_mirror_set_tunable32 METASLAB_FORCE_GANGING_PCT $force_pct
log_must zpool export $TESTPOOL
log_must has_gang $TESTPOOL/gang $gobj
log_must has_gang $TESTPOOL/nocksum $nobj
async_mirror_suspend_copy 0
log_must zpool import -l -d $ASYNC_MIRROR_DIR $TESTPOOL
block_device_wait $ZVOL_DEVDIR/$TESTPOOL/vol
log_must wait_async_mirror_idle
log_must test "$(get_async_mirror_stat failed)" == 0
log_must test "$(get_async_mirror_stat data_errors)" == 0
log_must zpool export $TESTPOOL

async_mirror_import_lagging
log_must zpool scrub -w $TESTPOOL
log_must check_pool_status $TESTPOOL errors "No known data errors"
log_must test "$(async_mirror_cksum_errors $ASYNC_MIRROR_LAGGING)" == 0
log_must test "$sums1" == "$(digests w1)"
log_must test "$sums2" == "$(digests w2; xxh128digest /$TESTPOOL/gang/g;
    xxh128digest /$TESTPOOL/nocksum/g)"
log_must zpool export $TESTPOOL
async_mirror_unhide_leader

log_pass "The copy reaches each kind of block and finds no false errors."
