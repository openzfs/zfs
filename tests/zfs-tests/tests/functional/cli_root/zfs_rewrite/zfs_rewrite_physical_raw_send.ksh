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

# DESCRIPTION:
#	Verify that a raw incremental send of an encrypted dataset includes
#	the blocks rewritten with zfs rewrite -P, so that the replica it is
#	received into stays readable, for a file system as for a volume.
#
# STRATEGY:
#	1. Create an encrypted file system holding a file, and an encrypted
#	   volume, snapshot them and raw send the snapshots to replicas.
#	2. Create an unencrypted dataset holding a file and snapshot it.
#	3. Rewrite both files and the volume with -P and snapshot the three
#	   datasets again.
#	4. Verify that the raw incremental streams of the encrypted datasets
#	   hold a WRITE record for every block of the file and of the volume.
#	5. Verify that non-raw incrementals of the encrypted datasets, and a
#	   raw incremental of the unencrypted one, still hold none.
#	6. Receive the raw incrementals, load the keys of the replicas, and
#	   verify that the file and the volume read back intact without any
#	   authentication ereport.

. $STF_SUITE/include/libtest.shlib

verify_runnable "global"

typeset -i blocks=32
typeset src=$TESTPOOL/$TESTFS1
typeset dst=$TESTPOOL/$TESTFS2
typeset plain=$TESTPOOL/$TESTFS3
typeset vol=$TESTPOOL/$TESTVOL
typeset vdst=$TESTPOOL/$TESTVOL1
typeset voldev=$ZVOL_DEVDIR/$vol
typeset vdstdev=$ZVOL_DEVDIR/$vdst
typeset keyfile=$TEST_BASE_DIR/rewrite_raw_send.key
typeset stream=$TEST_BASE_DIR/rewrite_raw_send.stream
typeset raw=$TEST_BASE_DIR/rewrite_raw_send.raw
typeset vraw=$TEST_BASE_DIR/rewrite_raw_send.vraw
typeset dump=$TEST_BASE_DIR/rewrite_raw_send.dump

function cleanup
{
	for ds in $dst $src $plain $vdst $vol; do
		datasetexists $ds && destroy_dataset $ds -r
	done
	rm -f $keyfile $stream $raw $vraw $dump
}

# Write an incremental stream to a file and verify how many WRITE records
# it holds for an object.
function check_writes # file object count send-arguments
{
	typeset file=$1
	typeset obj=$2
	typeset -i count=$3
	shift 3

	log_must eval "zfs send $* > $file"
	log_must eval "zstream dump -v < $file > $dump"
	typeset -i n=$(awk -v obj=$obj '$1 == "WRITE" && $4 == obj' $dump | \
	    wc -l)
	log_note "zfs send $*: $n WRITE records for object $obj"
	log_must test $n -eq $count
}

log_assert "zfs rewrite -P blocks are included in raw incremental sends" \
    "of encrypted datasets"
log_onexit cleanup

log_must eval "echo password > $keyfile"
log_must zfs create -o encryption=on -o keyformat=passphrase \
    -o keylocation=file://$keyfile -o recordsize=128k $src
typeset src_file=$(get_prop mountpoint $src)/file
log_must dd if=/dev/urandom of=$src_file bs=128k count=$blocks
typeset src_obj=$(get_objnum $src_file)
sync_pool $TESTPOOL
log_must zfs snapshot $src@a
log_must eval "zfs send -w $src@a > $stream"
log_must eval "zfs recv $dst < $stream"

# The data of a volume are its object 1.
log_must zfs create -o encryption=on -o keyformat=passphrase \
    -o keylocation=file://$keyfile -V $((blocks * 128))k \
    -o volblocksize=128k $vol
block_device_wait $voldev
log_must dd if=/dev/urandom of=$voldev bs=128k count=$blocks conv=fsync
sync_pool $TESTPOOL
log_must zfs snapshot $vol@a
log_must eval "zfs send -w $vol@a > $stream"
log_must eval "zfs recv $vdst < $stream"

log_must zfs create -o recordsize=128k $plain
typeset plain_file=$(get_prop mountpoint $plain)/file
log_must dd if=/dev/urandom of=$plain_file bs=128k count=$blocks
typeset plain_obj=$(get_objnum $plain_file)
sync_pool $TESTPOOL
log_must zfs snapshot $plain@a

log_must zfs rewrite -P $src_file $plain_file
log_must zfs rewrite -P $vol
sync_pool $TESTPOOL
log_must zfs snapshot $src@b $plain@b $vol@b

# Every rewritten block got a new IV and MAC, which a raw stream carries.
check_writes $raw $src_obj $blocks -w -i @a $src@b
check_writes $vraw 1 $blocks -w -i @a $vol@b

# Streams without encryption parameters keep omitting the rewritten blocks.
check_writes $stream $src_obj 0 -i @a $src@b
check_writes $stream 1 0 -i @a $vol@b
check_writes $stream $plain_obj 0 -w -i @a $plain@b

log_must zpool events -c
log_must eval "zfs recv $dst < $raw"
log_must zfs load-key -L file://$keyfile $dst
log_must zfs mount $dst
typeset dst_file=$(get_prop mountpoint $dst)/file
log_must cmp $src_file $dst_file
log_must eval "zfs recv $vdst < $vraw"
log_must zfs load-key -L file://$keyfile $vdst
block_device_wait $vdstdev
log_must cmp $voldev $vdstdev
if zpool events | grep -q "ereport.fs.zfs.authentication"; then
	zpool events -v
	log_fail "Reading the replica posted an authentication ereport"
fi

log_pass "zfs rewrite -P blocks are included in raw incremental sends" \
    "of encrypted datasets"
