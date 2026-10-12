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
#	Expansion must refuse a RAIDZ vdev carrying a parity-epoch table.
#
# STRATEGY:
#	1. Install a geometry-identical or mixed-parity epoch table.
#	2. Require attach to fail without activating RAIDZ expansion.
#	3. Check surviving data, scrub and reimport after the refused attach.
#

verify_runnable "global"

typeset pool=${TESTPOOL}_pe_expand
typeset workdir=$TEST_BASE_DIR/raidz_pe_expand.$$
typeset mnt=$workdir/mnt
typeset -a disks

function cleanup
{
	poolexists $pool && destroy_pool $pool
	rm -rf $workdir
}

log_assert "Parity-epoch RAIDZ expansion is refused before changing the pool"
log_onexit cleanup

log_must mkdir -p $workdir
for i in 0 1 2 3 4; do
	disks[i]=$workdir/vdev$i
	log_must truncate -s 256M ${disks[i]}
done
log_must dd if=/dev/urandom of=$workdir/expected bs=128k count=64

for parity in 1 2; do
	log_must zpool create -f -o ashift=12 -O mountpoint=$mnt \
	    -O compression=off $pool raidz1 ${disks[0]} ${disks[1]} \
	    ${disks[2]} ${disks[3]}
	log_must cp $workdir/expected $mnt/before
	sync_pool $pool
	log_must zpool export $pool

	typeset txg=$(zdb -lu ${disks[0]} | \
	    awk '/txg = / {if ($3 > max) max = $3} END {print max + 1}')
	log_must zhack -d $workdir raidz_epochs $pool 0 0:4:1 $txg:4:$parity
	log_must zpool import -d $workdir $pool
	log_must cp $workdir/expected $mnt/after
	sync_pool $pool

	log_mustnot zpool attach -w $pool raidz1-0 ${disks[4]}
	log_must eval "zpool get -H -o value feature@raidz_expansion \
	    $pool | grep -qx enabled"
	log_mustnot eval "zpool status -P $pool | grep -F ${disks[4]}"
	log_must cmp $workdir/expected $mnt/before
	log_must cmp $workdir/expected $mnt/after
	log_must zpool scrub -w $pool
	log_must check_pool_status $pool "errors" "No known data errors"
	log_must zpool export $pool
	log_must zpool import -d $workdir $pool
	log_must cmp $workdir/expected $mnt/before
	log_must cmp $workdir/expected $mnt/after
	log_must zpool destroy $pool
done

log_pass "Refused parity-epoch expansion preserves importability and data"
