#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# CDDL HEADER START
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
# CDDL HEADER END
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	zdb -R reads a block of an expanded raidz vdev with the width the
#	vdev had in the transaction group the t flag gives, and with the
#	width it had before its first expansion without it.
#
# STRATEGY:
#	1. Create a 3-wide raidz1 pool on file vdevs, write a block of
#	   random data, expand the vdev to 4 disks and write another one.
#	2. Note the offset, sizes, physical birth and checksum of both
#	   blocks from zdb -ddddd, and require their allocated sizes to
#	   differ, as they do when the second has the new width.
#	3. Require zdb -R to read the first block, with or without t.
#	4. Require zdb -R without the t flag to read other data than the
#	   second block's, as laid out with the width before the expansion.
#	5. Require zdb -R with the t flag and the physical birth of the
#	   second block to read it, whose fletcher4 checksum is then the one
#	   of its block pointer.
#	6. Require zdb -R to refuse a birth that is not only decimal digits.
#

verify_runnable "global"

typeset -r devs=4
typeset -r outfile=$TEST_BASE_DIR/zdb_read_block_birth.out
typeset -a disks
typeset mntpnt line off asize psize birth cksum got out t
typeset old_off old_asize old_psize old_birth old_cksum

function cleanup
{
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
	for i in {0..$((devs - 1))}; do
		rm -f $TEST_BASE_DIR/dev-$i
	done
	rm -f $outfile
}

# Set off, asize, psize, birth and cksum from the level-0 block of a file.
function block_of
{
	typeset obj=$(get_objnum $1)

	line=$(zdb -ddddd $TESTPOOL/ $obj |
	    awk '$2 == "L0" && $3 ~ /^0:/' | head -1)
	[[ -n $line ]] || log_fail "no level-0 block pointer for $1"
	log_note "$1: $line"
	off=$(print -r -- "$line" | awk '{ print $3 }' | cut -d: -f2)
	asize=$(print -r -- "$line" | awk '{ print $3 }' | cut -d: -f3)
	psize=$(print -r -- "$line" | sed -n 's/.*L\/\([0-9a-f]*\)P .*/\1/p')
	birth=$(print -r -- "$line" |
	    sed -n 's/.* B=[0-9]*\/\([0-9]*\) .*/\1/p')
	cksum=$(print -r -- "$line" | sed -n 's/.* cksum=\([0-9a-f:]*\).*/\1/p')
	[[ $off == +([0-9a-f]) && $asize == +([0-9a-f]) && \
	    $psize == +([0-9a-f]) && $birth == +([0-9]) && -n $cksum ]] ||
	    log_fail "cannot parse the block pointer of $1: $line"
}

# Set got to the fletcher4 checksum of what zdb -R reads.
function read_cksum
{
	log_must eval "zdb -R $TESTPOOL $1 > $outfile 2>&1"
	got=$(awk '$1 == "fletcher4" { sub("cksum=", "", $2); print $2 }' \
	    $outfile)
	[[ -n $got ]] || log_fail "no fletcher4 checksum from zdb -R $1"
}

log_onexit cleanup
log_assert "zdb -R reads an expanded raidz block with the width of its birth"

for i in {0..$((devs - 1))}; do
	log_must truncate -s $MINVDEVSIZE $TEST_BASE_DIR/dev-$i
	disks[$i]=$TEST_BASE_DIR/dev-$i
done

log_must zpool create -f -o ashift=12 -O compression=off \
    -O recordsize=128k $TESTPOOL raidz1 ${disks[0]} ${disks[1]} ${disks[2]}
mntpnt=$(get_prop mountpoint $TESTPOOL)
log_must dd if=/dev/urandom of=$mntpnt/before bs=128k count=1
sync_pool $TESTPOOL

log_must zpool attach -w $TESTPOOL raidz1-0 ${disks[3]}
# The new width applies from a few txgs after the expansion completes.
sync_pool $TESTPOOL
sync_pool $TESTPOOL
log_must dd if=/dev/urandom of=$mntpnt/after bs=128k count=1
sync_pool $TESTPOOL

block_of $mntpnt/before
old_off=$off old_asize=$asize old_psize=$psize old_birth=$birth
old_cksum=$cksum
block_of $mntpnt/after
[[ $asize != "$old_asize" ]] ||
    log_fail "the block born in $birth was allocated with the old width"

read_cksum 0:$old_off:$old_psize:c
[[ $got == "$old_cksum" ]] ||
    log_fail "zdb -R read $got for the block born before the expansion"
read_cksum 0:$old_off:$old_psize:ct$old_birth
[[ $got == "$old_cksum" ]] ||
    log_fail "zdb -R with t$old_birth read $got, the block has $old_cksum"

read_cksum 0:$off:$psize:c
[[ $got != "$cksum" ]] ||
    log_fail "without t, zdb -R read the block with its own width"
read_cksum 0:$off:$psize:ct$birth
[[ $got == "$cksum" ]] ||
    log_fail "zdb -R with t$birth read $got, the block has $cksum"

# A sign, a space, no digit or a zero is not a birth.
for t in "t-1" "t+$birth" "t $birth" "tx" "t0"; do
	out=$(zdb -R $TESTPOOL "0:$off:$psize:c$t" 2>&1)
	[[ $out == *"Invalid birth txg"* ]] ||
	    log_fail "zdb -R took the birth in c$t: ${out:0:200}"
done

log_pass "zdb -R reads an expanded raidz block with the width of its birth"
