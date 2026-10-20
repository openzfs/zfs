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

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	A resilver rebuilds every child that a block of a narrower
#	parity epoch spans.  Such a block is laid out over more rows than
#	a block of the vdev's own width, each row with its own parity
#	columns, so it spans more consecutive children than its sectors
#	plus one row of parity.
#
# STRATEGY:
#	For an eight-wide RAIDZ1 with a three-wide parity-1 epoch, a
#	five-wide RAIDZ1 with a three-wide parity-2 epoch, an eight-wide
#	RAIDZ2 with a four-wide parity-2 epoch and an eight-wide RAIDZ3
#	with a five-wide parity-3 epoch, with records that need two rows
#	of the epoch:
#	1. Store the epoch with zhack and write the data in it.
#	2. Replace the last child and wait for the resilver.
#	3. Require a scrub to find nothing to repair, and the data to
#	   read back.
#

verify_runnable "global"

typeset pool=${TESTPOOL}_pe_resilver
typeset workdir=$TEST_BASE_DIR/raidz_pe_resilver.$$
typeset mnt=$workdir/mnt

function cleanup
{
	poolexists $pool && destroy_pool $pool
	rm -rf $workdir
}

# Sum of the CKSUM column of every vdev of the pool.
function cksum_errors
{
	zpool status -p $pool | awk '
	    !NF { isvdev = 0 }
	    isvdev { errors += $NF }
	    /CKSUM$/ { isvdev = 1 }
	    END { print errors + 0 }'
}

# base_parity width narrow_width narrow_parity recordsize
function check_resilver
{
	typeset -i base=$1
	typeset -i width=$2
	typeset -i narrow=$3
	typeset -i parity=$4
	typeset recsize=$5
	typeset -a disks
	typeset -i i

	log_note "$width-wide raidz$base, epoch $narrow wide with parity" \
	    "$parity, recordsize $recsize"

	for ((i = 0; i < width; i++)); do
		disks[i]=$workdir/vdev$i
		log_must truncate -s 256M ${disks[i]}
	done
	log_must truncate -s 256M $workdir/spare

	log_must zpool create -f -o ashift=12 -O mountpoint=$mnt \
	    -O compression=off -O recordsize=$recsize \
	    $pool raidz$base ${disks[@]}
	log_must zpool export $pool
	log_must zhack -d $workdir raidz_epochs $pool 0 \
	    0:$width:$base next:$narrow:$parity
	log_must zpool import -d $workdir $pool

	log_must dd if=/dev/urandom of=$workdir/expected bs=$recsize \
	    count=4096
	log_must cp $workdir/expected $mnt/file
	sync_pool $pool

	log_must zpool replace -w $pool ${disks[width - 1]} $workdir/spare
	log_must zpool scrub -w $pool

	typeset -i errors=$(cksum_errors)
	if ((errors != 0)) || \
	    ! check_pool_status $pool "scan" "repaired 0B"; then
		log_note "$(zpool status -p $pool)"
		log_fail "the scrub repaired what the resilver skipped" \
		    "($errors checksum errors)"
	fi
	log_must check_pool_status $pool "errors" "No known data errors"
	log_must cmp $workdir/expected $mnt/file

	log_must destroy_pool $pool
	log_must rm -f ${disks[@]} $workdir/spare $workdir/expected
}

log_assert "A resilver rebuilds every child a narrower-epoch block spans"
log_onexit cleanup

log_must mkdir -p $workdir

check_resilver 1 8 3 1 16k
check_resilver 1 5 3 2 8k
check_resilver 2 8 4 2 16k
check_resilver 3 8 5 3 16k

log_pass "A resilver rebuilds every child a narrower-epoch block spans"
