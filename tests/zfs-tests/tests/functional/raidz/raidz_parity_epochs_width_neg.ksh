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
#	An epoch width cannot exceed the RAIDZ vdev's physical child count.
#
# STRATEGY:
#	1. Write oversized future epochs on an exported four-wide RAIDZ1.
#	2. Require userspace and kernel imports to reject the table.
#	3. Restore valid tables, allow narrower widths, and check data and scrub.
#

verify_runnable "global"

typeset pool=${TESTPOOL}_pe_width
typeset workdir=$TEST_BASE_DIR/raidz_pe_width.$$
typeset mnt=$workdir/mnt
typeset -a disks

function cleanup
{
	poolexists $pool && destroy_pool $pool
	rm -rf $workdir
}

log_assert "Oversized parity epochs fail import before influencing I/O"
log_onexit cleanup

log_must mkdir -p $workdir
for i in 0 1 2 3; do
	disks[i]=$workdir/vdev$i
	log_must truncate -s 256M ${disks[i]}
done
log_must zpool create -f -O mountpoint=$mnt -O compression=off \
    $pool raidz1 ${disks[@]}
log_must dd if=/dev/urandom of=$workdir/expected bs=128k count=64
log_must cp $workdir/expected $mnt/file
sync_pool $pool
log_must zpool export $pool

# A future start keeps the intentionally malformed table out of the debug
# writer's own sync I/O. The loader must reject it even before it is used.
for width in 6 256; do
	log_must zhack -d $workdir raidz_epochs $pool 0 \
	    0:4:1 18446744073709551615:$width:1
	log_mustnot zdb -e -p $workdir -m $pool
	log_mustnot zpool import -d $workdir $pool
	log_must zhack -o raidz_ignore_parity_epochs=1 -d $workdir \
	    raidz_epochs $pool 0 0:4:1
	log_must zpool import -d $workdir $pool
	log_must cmp $workdir/expected $mnt/file
	log_must zpool scrub -w $pool
	log_must check_pool_status $pool "errors" "No known data errors"
	log_must zpool export $pool
done

# Logical widths below the physical width remain supported.
log_must zhack -d $workdir raidz_epochs $pool 0 \
    0:4:1 18446744073709551615:3:1
log_must zpool import -d $workdir $pool
log_must cmp $workdir/expected $mnt/file
log_must zpool scrub -w $pool
log_must check_pool_status $pool "errors" "No known data errors"

log_pass "Oversized epochs are refused and valid tables preserve data"
