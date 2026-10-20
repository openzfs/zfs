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
#	Verify zfs rewrite -W rewrites the blocks that a change of the
#	compression, checksum, copies, special_small_blocks or dedup property
#	would change, and skips them once they are current.
#
# STRATEGY:
#	1. Create a pool with a special vdev and lz4 compression, and write a
#	   compressible file Z and an incompressible file R.
#	2. Verify -W moves no block.
#	3. Change one property at a time, and verify that -W moves the blocks
#	   of the files that the change affects, and only those, then that a
#	   second -W moves nothing:
#	   - compression=zstd and compression=off move Z only, since R is
#	     stored uncompressed,
#	   - checksum=sha256 and copies=2 move both,
#	   - special_small_blocks=128K moves both to the special vdev,
#	   - dedup=on moves both,
#	   - copies=1 after dedup=on moves neither, since the flat DDT entries
#	     keep two DVAs.
#	4. Verify the data and scrub the pool.

. $STF_SUITE/include/libtest.shlib

verify_runnable "global"

typeset -r dev_size_mb=256
typeset dev0=$TEST_BASE_DIR/dev-0
typeset dev1=$TEST_BASE_DIR/dev-1
typeset tmp=$(mktemp)
typeset z0=$(mktemp)
typeset z1=$(mktemp)
typeset r0=$(mktemp)
typeset r1=$(mktemp)

function cleanup
{
	poolexists $TESTPOOL1 && destroy_pool $TESTPOOL1
	rm -f $dev0 $dev1 $tmp $z0 $z1 $r0 $r1
	rm -rf $TESTDIR1
}

function save_dvas # file output
{
	log_must eval "zdb -Ovv $TESTPOOL1 $1 > $tmp"
	log_must eval "awk '/ L0 / { print \$3 }' < $tmp > $2"
	log_must test $(wc -l < $2) -eq 8
}

function check_moved # file1 file2
{
	typeset same=$(sort $1 $2 | uniq -d)
	log_must [ -z "$same" ]
}

# Rewrite both files with -W and check which of them moved, then rewrite
# them with -W again and check that nothing moved.
function rewrite_current # Z-moves R-moves
{
	save_dvas Z $z0
	save_dvas R $r0
	log_must zfs rewrite -W $TESTDIR1/Z $TESTDIR1/R
	log_must sync_pool $TESTPOOL1
	save_dvas Z $z1
	save_dvas R $r1
	if [ $1 = yes ]; then
		check_moved $z0 $z1
	else
		log_must cmp $z0 $z1
	fi
	if [ $2 = yes ]; then
		check_moved $r0 $r1
	else
		log_must cmp $r0 $r1
	fi

	log_must zfs rewrite -W $TESTDIR1/Z $TESTDIR1/R
	log_must sync_pool $TESTPOOL1
	save_dvas Z $z0
	save_dvas R $r0
	log_must cmp $z0 $z1
	log_must cmp $r0 $r1
}

log_assert "zfs rewrite -W rewrites the blocks a property change affects"

log_onexit cleanup

log_must truncate -s ${dev_size_mb}M $dev0 $dev1
log_must zpool create -f -o ashift=12 -O compression=lz4 -O recordsize=128k \
    -m $TESTDIR1 $TESTPOOL1 $dev0 special $dev1

log_must eval "seq -w 1 200000 | head -c 1048576 > $TESTDIR1/Z"
log_must dd if=/dev/urandom of=$TESTDIR1/R bs=128k count=8
log_must sync_pool $TESTPOOL1
typeset hash_z=$(xxh128digest $TESTDIR1/Z)
typeset hash_r=$(xxh128digest $TESTDIR1/R)

rewrite_current no no

log_must zfs set compression=zstd $TESTPOOL1
rewrite_current yes no
log_must zfs set compression=off $TESTPOOL1
rewrite_current yes no

log_must zfs set checksum=sha256 $TESTPOOL1
rewrite_current yes yes
log_must zfs set copies=2 $TESTPOOL1
rewrite_current yes yes

log_must zfs set special_small_blocks=128K $TESTPOOL1
rewrite_current yes yes
typeset other=$(awk -F: '$1 != 1' $z1 $r1)
log_must [ -z "$other" ]

log_must zfs set dedup=on $TESTPOOL1
rewrite_current yes yes

# A rewrite would give the deduplicated blocks the two DVAs of their flat
# DDT entries again, so -W must skip them without a DDT lookup.
log_must zfs set copies=1 $TESTPOOL1
typeset -r lookup=ddt_stats_sha256.lookup
typeset lookups
lookups=$(kstat_pool $TESTPOOL1 $lookup)
rewrite_current no no
log_must test "$(kstat_pool $TESTPOOL1 $lookup)" -eq "$lookups"

log_must [ "$(xxh128digest $TESTDIR1/Z)" = "$hash_z" ]
log_must [ "$(xxh128digest $TESTDIR1/R)" = "$hash_r" ]
log_must zpool scrub -w $TESTPOOL1
log_must check_pool_status $TESTPOOL1 "errors" "No known data errors"

log_pass "zfs rewrite -W rewrites the blocks a property change affects"
