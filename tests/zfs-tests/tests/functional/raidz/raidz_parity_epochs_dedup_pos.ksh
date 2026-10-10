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
#	DDT extension must not combine copies with different parity in one
#	block pointer, even when their logical widths match.
#
# STRATEGY:
#	1. Write a deduplicated block in a five-wide RAIDZ1 pool.
#	2. Inject a parity-2 epoch after all existing blocks, then write
#	   the same data with copies=2. Require an ordinary two-copy block
#	   allocated entirely at parity 2.
#	3. Within the parity-2 epoch, require DDT extension to remain enabled.
#	4. Scrub, reimport, and compare all data.
#

verify_runnable "global"

typeset pool=${TESTPOOL}_pe_ddt
typeset workdir=$TEST_BASE_DIR/raidz_pe_ddt.$$
typeset mnt=$workdir/mnt
typeset -a disks

function cleanup
{
	poolexists $pool && destroy_pool $pool
	rm -rf $workdir
}

function check_bp
{
	typeset bp
	bp=$(zdb -ddddddbbbbbb $pool/ $(get_objnum $mnt/$1) |
	    grep -m 1 'L0 DVA')
	log_note "$1: $bp"
	[[ "$bp" =~ $2 ]] || log_fail "unexpected block pointer for $1"
	if [[ $3 == ordinary && "$bp" == *dedup* ]]; then
		log_fail "DDT extension combined different parity epochs"
	fi
}

log_assert "DDT extension requires matching RAIDZ width and parity"
log_onexit cleanup
log_must mkdir -p $workdir
for i in 0 1 2 3 4; do
	disks[i]=$workdir/vdev$i
	log_must truncate -s 256M ${disks[i]}
done

log_must zpool create -f -o ashift=12 -o feature@fast_dedup=enabled \
    -O mountpoint=$mnt -O dedup=sha256 -O compression=off \
    -O recordsize=128k $pool raidz1 ${disks[@]}
log_must dd if=/dev/urandom of=$mnt/p1 bs=128k count=1
sync_pool $pool
check_bp p1 'dedup single '
log_must zpool export $pool

typeset -i txg=$(zdb -lu ${disks[0]} |
    awk '/txg = /{print $3}' | sort -n | tail -1)
log_must test $txg -gt 0
log_must zhack -d $workdir raidz_epochs $pool 0 0:5:1 $((txg + 1)):5:2
log_must zpool import -d $workdir $pool

log_must zfs set copies=2 $pool
log_must dd if=$mnt/p1 of=$mnt/p2 bs=128k count=1
sync_pool $pool
check_bp p2 \
    'DVA\[0\]=<0:[0-9a-f]+:36000> DVA\[1\]=<0:[0-9a-f]+:36000>' ordinary

# Extension within the new epoch still deduplicates the two-copy write.
log_must zfs set copies=1 $pool
log_must dd if=/dev/urandom of=$mnt/p2-new bs=128k count=1
sync_pool $pool
check_bp p2-new 'dedup single '
log_must zfs set copies=2 $pool
log_must dd if=$mnt/p2-new of=$mnt/p2-new-copy bs=128k count=1
sync_pool $pool
check_bp p2-new-copy 'dedup double '

log_must zpool scrub -w $pool
log_must check_pool_status $pool "errors" "No known data errors"
log_must zpool export $pool
log_must zpool import -d $workdir $pool
log_must zpool scrub -w $pool
log_must check_pool_status $pool "errors" "No known data errors"
log_must cmp $mnt/p1 $mnt/p2
log_must cmp $mnt/p2-new $mnt/p2-new-copy

log_pass "DDT extension respects parity epochs and preserves data"
