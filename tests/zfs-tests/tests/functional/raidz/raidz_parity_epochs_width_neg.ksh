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
#	Every epoch of a parity-epoch table has the RAIDZ vdev's physical
#	width.  A wider row has more columns than the vdev has children, and a
#	narrower one describes a vdev that was expanded or contracted, which
#	cannot be combined with a table.
#
# STRATEGY:
#	1. Write future epochs of other widths on an exported four-wide RAIDZ1
#	   with zhack -f, since zhack refuses them.
#	2. Require userspace and kernel imports to reject the table, and the
#	   rescue tunable to allow only a read-only import.
#	3. Accept a table of the vdev's width, and check data and scrub.
#

verify_runnable "global"

typeset pool=${TESTPOOL}_pe_width
typeset workdir=$TEST_BASE_DIR/raidz_pe_width.$$
typeset mnt=$workdir/mnt
typeset -a disks

function cleanup
{
	set_tunable32 RAIDZ_IGNORE_PARITY_EPOCHS 0
	poolexists $pool && destroy_pool $pool
	rm -rf $workdir
}

# A fresh pool holding the expected data, exported.
function make_pool
{
	poolexists $pool && destroy_pool $pool
	log_must zpool create -f -O mountpoint=$mnt -O compression=off \
	    $pool raidz1 ${disks[@]}
	log_must cp $workdir/expected $mnt/file
	sync_pool $pool
	log_must zpool export $pool
}

log_assert "Epochs of another width fail import before influencing I/O"
log_onexit cleanup

log_must mkdir -p $workdir
for i in 0 1 2 3; do
	disks[i]=$workdir/vdev$i
	log_must truncate -s 256M ${disks[i]}
done
log_must dd if=/dev/urandom of=$workdir/expected bs=128k count=64

# The loader must reject the table even before any block uses it.  A pool
# whose table is damaged can only be read around, so each case gets its own.
for width in 6 256 3; do
	make_pool
	log_mustnot zhack -d $workdir raidz_epochs $pool 0 \
	    0:4:1 18446744073709551615:$width:1
	log_must zhack -d $workdir raidz_epochs -f $pool 0 \
	    0:4:1 18446744073709551615:$width:1
	log_mustnot zdb -e -p $workdir -m $pool
	log_mustnot zpool import -d $workdir $pool

	log_must set_tunable32 RAIDZ_IGNORE_PARITY_EPOCHS 1
	log_mustnot zpool import -d $workdir $pool
	log_must zpool import -o readonly=on -d $workdir $pool
	log_must cmp $workdir/expected $mnt/file
	log_must zpool export $pool
	log_must set_tunable32 RAIDZ_IGNORE_PARITY_EPOCHS 0
done

# A table of the vdev's own width is accepted.
make_pool
log_must zhack -d $workdir raidz_epochs $pool 0 \
    0:4:1 18446744073709551615:4:2
log_must zpool import -d $workdir $pool
log_must cmp $workdir/expected $mnt/file
log_must zpool scrub -w $pool
log_must check_pool_status $pool "errors" "No known data errors"

log_pass "Epochs of another width are refused and valid tables preserve data"
