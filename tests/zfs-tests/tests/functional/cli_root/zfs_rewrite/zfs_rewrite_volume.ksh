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
#	Verify zfs rewrite rewrites the blocks of volumes.
#
# STRATEGY:
#	1. Create a volume and fill it with data.
#	2. Rewrite parts of the volume by name, aligned, across the boundary
#	   of the pieces a volume is rewritten in, and not aligned.  Verify
#	   the data are the same, only the blocks of the part have changed,
#	   and they have as soon as the command returns.
#	3. Rewrite all of it, then physically: verify the logical birth time
#	   of the blocks is updated by the former and kept by the latter.
#	4. Verify -S skips the blocks shared with a snapshot, and only those.
#	5. Verify that file systems are refused, and that snapshots, read-only
#	   volumes, names that do not exist and devices that are not volumes
#	   are refused, and why.  Verify that encrypted volumes are rewritten,
#	   but not physically.
#	6. Rewrite a volume that has no device node, and one named by its
#	   device.
#	7. Rewrite a sparse volume without compression, and verify its holes
#	   stay holes.
#	8. Rewrite the volume repeatedly, also physically, while it is written
#	   to, and verify that no write is lost, nor keeps an old birth time.
#	9. Rewrite a volume that is held open, then roll it back, which fails
#	   if the rewrite left the volume held.

. $STF_SUITE/include/libtest.shlib

verify_runnable "global"

typeset vol=$TESTPOOL/${TESTVOL}_rewrite
typeset sparse=$vol-sparse
typeset dev=$ZVOL_DEVDIR/$vol
typeset sparsedev=$ZVOL_DEVDIR/$sparse
typeset evol=$vol-encrypted
typeset evoldev=$ZVOL_DEVDIR/$evol
typeset notavol=$TESTDIR/notavol
typeset -i blksz=16384
typeset -i size=32	# MiB
typeset -i nblocks
((nblocks = size * 1024 * 1024 / blksz))
typeset bps=$(mktemp)
typeset bps1=$(mktemp)
typeset snapbps=$(mktemp)
typeset shadow=$(mktemp)
typeset pat=$(mktemp)
typeset holder=""
typeset wpid=""

function cleanup
{
	[[ -n "$wpid" ]] && kill $wpid 2>/dev/null
	[[ -n "$holder" ]] && kill $holder 2>/dev/null
	wait
	rm -f $bps $bps1 $snapbps $shadow $pat $pat.0 $pat.1 $pat.2 $pat.3 \
	    $pat.4 $notavol
	datasetexists $sparse && destroy_dataset $sparse -R
	datasetexists $evol && destroy_dataset $evol -R
	datasetexists $vol && destroy_dataset $vol -R
}

# Save the offset, first DVA and logical birth time of every data block of a
# volume, and verify there are as many as expected, if a number is given.
# Holes that have a birth time, as freed blocks do, are not data blocks.
function save_blocks # dataset file [count]
{
	typeset out=$(mktemp) || return 1
	typeset -i n

	zdb -vvvvv $1 1 > $out || { rm -f $out; return 1; }
	awk '/ L0 / && !/ HOLE / && !/ 0:0:0 / { b = ""
	    if (match($0, /birth=[0-9]+L/))
	        b = substr($0, RSTART + 6, RLENGTH - 7)
	    print $1 " " $3 " " b }' < $out > $2
	rm -f $out
	[[ -z "$3" ]] && return 0
	n=$(wc -l < $2)
	((n == $3)) || log_note "$1 has $n data blocks, not $3"
	((n == $3))
}

# Verify that zfs rewrite fails with the given error, which tells what
# refused it.
function rewrite_fails # errno arguments...
{
	typeset -i want=$1

	shift
	zfs rewrite "$@"
	(($? == want))
}

# Print the offsets of the blocks that differ between two saved lists.
function changed_blocks # list1 list2
{
	echo $(sort $1 $2 | uniq -u | cut -f1 -d' ' | sort -u)
}

# Print the offsets of the blocks first to last, in the same order.
function block_offsets # first last
{
	typeset -i i

	for ((i = $1; i <= $2; i++)); do
		printf "%x\n" $((i * blksz))
	done | sort | tr '\n' ' ' | sed 's/ $//'
	echo
}

# Print the number of blocks that are the same in two saved lists.
function same_blocks # list1 list2
{
	sort $1 $2 | uniq -d | wc -l
}

# Verify that all the blocks have the same logical birth time, or that none
# has, in two saved lists of the same blocks.
function logical_births # same|updated list1 list2
{
	awk -v want=$1 '
	    NR == FNR { birth[FNR] = $3; n = NR; next }
	    { if ($3 == "" || birth[FNR] == "" ||
	          (birth[FNR] == $3) != (want == "same")) bad++ }
	    END { exit (bad != 0 || n == 0 || FNR != n) }' $2 $3
}

# Print the checksum of the data of a device, as the volume holds it: what the
# kernel has cached for the device must not be mistaken for it.
function dev_hash # device
{
	if is_linux; then
		blockdev --flushbufs $1 || return 1
	fi
	xxh128digest $1
}

# Write patterns to the volume, and to a file that tracks what it must hold.
# Each slot gets a pattern that is not the one the previous round left in it.
function writer # round
{
	typeset -i r

	for ((r = 0; r < size; r++)); do
		typeset p=$pat.$(((r + $1) % 5))

		dd if=$p of=$dev bs=1M seek=$r count=1 conv=fsync \
		    2>/dev/null || return 1
		dd if=$p of=$shadow bs=1M seek=$r count=1 conv=notrunc \
		    2>/dev/null || return 1
	done
	return 0
}

log_assert "zfs rewrite rewrites the blocks of volumes"

log_onexit cleanup

log_must zfs create -V ${size}m -o volblocksize=$blksz $vol
block_device_wait $dev
log_must dd if=/dev/urandom of=$dev bs=1M count=$size conv=fsync
log_must sync_pool $TESTPOOL
typeset orig_hash=$(dev_hash $dev)
log_must [ -n "$orig_hash" ]
log_must save_blocks $vol $bps1 $nblocks

# A part of the volume, that is aligned: 128 blocks, from the 64th.
log_must zfs rewrite -o 1048576 -l 2097152 $vol
log_must save_blocks $vol $bps $nblocks
log_must [ "$(changed_blocks $bps $bps1)" = "$(block_offsets 64 191)" ]
log_must [ "$orig_hash" = "$(dev_hash $dev)" ]

# One that crosses the boundary of the pieces: 128 blocks, from the 192nd.
log_must cp $bps $bps1
log_must zfs rewrite -o 3145728 -l 2097152 $vol
log_must save_blocks $vol $bps $nblocks
log_must [ "$(changed_blocks $bps $bps1)" = "$(block_offsets 192 319)" ]

# One that is not aligned: 7 blocks, from the 61st.
log_must cp $bps $bps1
log_must zfs rewrite -o 1000000 -l 100000 $vol
log_must save_blocks $vol $bps $nblocks
log_must [ "$(changed_blocks $bps $bps1)" = "$(block_offsets 61 67)" ]
log_must [ "$orig_hash" = "$(dev_hash $dev)" ]

# All of it, and the names of the volumes printed.  The data must be on stable
# storage when the command returns, so no sync here: zdb reads the pool as it
# is on disk.
log_must cp $bps $bps1
log_must [ "$(zfs rewrite -v $vol)" = "$vol" ]
log_must save_blocks $vol $bps $nblocks
log_must [ "$(same_blocks $bps $bps1)" -eq 0 ]
log_must logical_births updated $bps1 $bps
log_must [ "$orig_hash" = "$(dev_hash $dev)" ]

# Physically: the blocks move, as if they had not been written.
log_must cp $bps $bps1
log_must zfs rewrite -P $vol
log_must save_blocks $vol $bps $nblocks
log_must [ "$(same_blocks $bps $bps1)" -eq 0 ]
log_must logical_births same $bps1 $bps
log_must [ "$orig_hash" = "$(dev_hash $dev)" ]

# Blocks shared with a snapshot: skipped by -S, and only those.
log_must zfs snapshot $vol@snap
log_must save_blocks $vol@snap $snapbps $nblocks
log_must dd if=/dev/urandom of=$dev bs=1M seek=2 count=1 conv=fsync
log_must sync_pool $TESTPOOL
orig_hash=$(dev_hash $dev)
log_must [ -n "$orig_hash" ]
log_must save_blocks $vol $bps1 $nblocks
log_must zfs rewrite -S $vol
log_must save_blocks $vol $bps $nblocks
log_must [ "$(changed_blocks $bps $bps1)" = "$(block_offsets 128 191)" ]
log_must [ "$(same_blocks $bps $snapbps)" -eq $((nblocks - 64)) ]
log_must zfs rewrite $vol
log_must save_blocks $vol $bps $nblocks
log_must [ "$(same_blocks $bps $snapbps)" -eq 0 ]
log_must [ "$orig_hash" = "$(dev_hash $dev)" ]
log_must zfs destroy $vol@snap

# What can not be rewritten: ENOENT, EROFS and EINVAL tell what refused it.
log_must rewrite_fails 2 $TESTPOOL/${TESTVOL}_does_not_exist
# A file system is not a volume, and its name is no path either.
log_must rewrite_fails 2 $TESTPOOL/$TESTFS
log_must zfs snapshot $vol@snap
log_must rewrite_fails 30 $vol@snap
log_must zfs set snapdev=visible $vol
block_device_wait $dev@snap
log_must rewrite_fails 30 $dev@snap
log_must zfs inherit snapdev $vol
log_must zfs destroy $vol@snap
log_must zfs set readonly=on $vol
log_must rewrite_fails 30 $vol
log_must zfs set readonly=off $vol
log_must rewrite_fails 22 $ZVOL_DEVDIR/$TESTPOOL
log_must ln -s /dev/null $notavol
log_must rewrite_fails 22 $notavol

# An encrypted volume: rewritten, but not physically.
log_must eval "echo password | zfs create -o encryption=on \
    -o keyformat=passphrase -o keylocation=prompt -V 8m \
    -o volblocksize=$blksz $evol"
block_device_wait $evoldev
log_must dd if=/dev/urandom of=$evoldev bs=1M count=8 conv=fsync
log_must sync_pool $TESTPOOL
typeset evol_hash=$(dev_hash $evoldev)
log_must [ -n "$evol_hash" ]
log_must save_blocks $evol $bps1 $((8 * 1048576 / blksz))
log_mustnot zfs rewrite -P $evol
log_must zfs rewrite $evol
log_must save_blocks $evol $bps $((8 * 1048576 / blksz))
log_must [ "$(same_blocks $bps $bps1)" -eq 0 ]
log_must logical_births updated $bps1 $bps
log_must [ "$evol_hash" = "$(dev_hash $evoldev)" ]
log_must destroy_dataset $evol

# A volume that has no device node, and one that is named by its device.
log_must zfs set volmode=none $vol
log_must save_blocks $vol $bps1 $nblocks
log_must zfs rewrite $vol
log_must save_blocks $vol $bps $nblocks
log_must [ "$(same_blocks $bps $bps1)" -eq 0 ]
log_must zfs inherit volmode $vol
block_device_wait $dev
log_must [ "$orig_hash" = "$(dev_hash $dev)" ]
log_must cp $bps $bps1
log_must zfs rewrite $dev
log_must save_blocks $vol $bps $nblocks
log_must [ "$(same_blocks $bps $bps1)" -eq 0 ]

# A sparse volume, without compression, which would turn zeroes written back
# into holes again: its holes stay holes.
log_must zfs create -s -V ${size}m -o volblocksize=$blksz -o compression=off \
    $sparse
block_device_wait $sparsedev
log_must dd if=/dev/urandom of=$sparsedev bs=1M seek=4 count=8 conv=fsync
log_must sync_pool $TESTPOOL
typeset -i sparse_blocks
((sparse_blocks = 8 * 1024 * 1024 / blksz))
log_must save_blocks $sparse $bps1 $sparse_blocks
log_must zfs rewrite $sparse
log_must save_blocks $sparse $bps $sparse_blocks
log_must [ "$(same_blocks $bps $bps1)" -eq 0 ]
log_must [ "$(cut -f1 -d' ' $bps | tr '\n' ' ')" = \
    "$(cut -f1 -d' ' $bps1 | tr '\n' ' ')" ]
log_must destroy_dataset $sparse

# Rewrite again and again while the volume is written to, in rounds, each one
# leaving a different pattern in every slot, so that a stale slot can not be
# mistaken for the right one.  Every other round rewrites physically: as every
# slot is written in every round, every block must then have a newer birth
# time, which a write taken for the physical rewrite would not have.
for i in 0 1 2 3 4; do
	log_must dd if=/dev/urandom of=$pat.$i bs=1M count=1
done
log_must dd if=$dev of=$shadow bs=1M
typeset -i round
for ((round = 0; round < 4; round++)); do
	typeset opt=""

	if ((round % 2 == 1)); then
		opt="-P"
		log_must sync_pool $TESTPOOL
		log_must save_blocks $vol $bps1 $nblocks
	fi
	writer $round &
	wpid=$!
	typeset -i tries=0
	while kill -0 $wpid 2>/dev/null; do
		log_must zfs rewrite $opt $vol
		((tries += 1))
		((tries < 100)) || log_fail "the writer does not finish"
	done
	wait $wpid
	log_must [ $? -eq 0 ]
	wpid=""
	typeset shadow_hash=$(xxh128digest $shadow)
	log_must [ -n "$shadow_hash" ]
	log_must [ "$shadow_hash" = "$(dev_hash $dev)" ]
	if [[ -n "$opt" ]]; then
		log_must sync_pool $TESTPOOL
		log_must save_blocks $vol $bps $nblocks
		log_must logical_births updated $bps1 $bps
	fi
done

# Rewrite a volume that is held open, then roll it back: that fails if the
# rewrite left the volume held.
log_must zfs snapshot $vol@open
orig_hash=$(dev_hash $dev)
log_must [ -n "$orig_hash" ]
sleep 300 < $dev &
holder=$!
log_must zfs rewrite $vol
log_must [ "$orig_hash" = "$(dev_hash $dev)" ]
log_must dd if=/dev/urandom of=$dev bs=1M count=1 conv=fsync
log_must zfs rewrite $vol
log_must zfs rollback $vol@open
log_must [ "$orig_hash" = "$(dev_hash $dev)" ]
kill $holder
wait $holder
holder=""

log_pass "zfs rewrite rewrites the blocks of volumes"
