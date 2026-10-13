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
#	After a RAID-Z vdev has been expanded more than once, a resilver
#	rebuilds every child that a block born before the expansions
#	spans.  Such a block keeps its narrower logical width, so it is
#	laid out over more rows, each with its own parity, and spans more
#	consecutive children than its sectors plus one row of parity.
#
# STRATEGY:
#	For a four-wide RAIDZ1, a five-wide RAIDZ2 and a six-wide RAIDZ3:
#	1. Write 16K blocks.
#	2. Expand the vdev twice.
#	3. Replace each child in turn; after each resilver, require a
#	   scrub to find nothing to repair, and the data to read back.
#

verify_runnable "global"

typeset pool=$TESTPOOL
typeset workdir=$TEST_BASE_DIR/raidz_expand_010.$$
typeset mnt=$workdir/mnt

function cleanup
{
	poolexists $pool && destroy_pool $pool
	rm -rf $workdir
	log_must restore_tunable SCRUB_AFTER_EXPAND
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

# parity width
function check_expand
{
	typeset -i parity=$1
	typeset -i width=$2
	typeset -a disks
	typeset -i i

	log_note "$width-wide raidz$parity expanded twice"

	for ((i = 0; i < width + 2; i++)); do
		disks[i]=$workdir/vdev$i
		log_must truncate -s 256M ${disks[i]}
	done

	log_must zpool create -f -o ashift=12 \
	    -o feature@raidz_expansion=enabled -O mountpoint=$mnt \
	    -O compression=off -O recordsize=16k \
	    $pool raidz$parity ${disks[@]:0:width}
	log_must dd if=/dev/urandom of=$workdir/expected bs=16k count=4096
	log_must cp $workdir/expected $mnt/file
	sync_pool $pool

	for ((i = width; i < width + 2; i++)); do
		log_must zpool attach $pool raidz$parity-0 ${disks[i]}
		log_must zpool wait -t raidz_expand $pool
	done

	for ((i = 0; i < width + 2; i++)); do
		log_must truncate -s 256M $workdir/new$i
		log_must zpool replace -w $pool ${disks[i]} $workdir/new$i
		disks[i]=$workdir/new$i
		log_must zpool scrub -w $pool

		typeset -i errors=$(cksum_errors)
		if ((errors != 0)) || \
		    ! check_pool_status $pool "scan" "repaired 0B"; then
			log_note "$(zpool status -p $pool)"
			log_fail "the scrub after replacing child $i repaired" \
			    "what the resilver skipped ($errors checksum errors)"
		fi
	done
	log_must check_pool_status $pool "errors" "No known data errors"
	log_must cmp $workdir/expected $mnt/file

	log_must destroy_pool $pool
	log_must rm -f $workdir/vdev* $workdir/new* $workdir/expected
}

log_assert "A resilver rebuilds every child a block born before two" \
    "RAID-Z expansions spans"
log_onexit cleanup
log_must save_tunable SCRUB_AFTER_EXPAND
log_must set_tunable32 SCRUB_AFTER_EXPAND 0

log_must mkdir -p $workdir

check_expand 1 4
check_expand 2 5
check_expand 3 6

log_pass "A resilver rebuilds every child a block born before two" \
    "RAID-Z expansions spans"
