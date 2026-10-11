#!/bin/ksh
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

#
# Description:
# A DEGRADED single-disk top-level vdev must not turn every write into
# a gang block when no healthy vdev can take the block instead, while
# a healthy vdev is still preferred when there is one.
#
# Strategy:
# 1. Create a pool with one single-disk top-level vdev and degrade it.
# 2. Write a file of 128K blocks and verify none of its L0 or L1 blocks
#    are gang blocks.
# 3. Create a pool with two single-disk top-level vdevs, degrade one,
#    write a file and verify its blocks are not ganged and are all on
#    the healthy vdev.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/gang_blocks/gang_blocks.kshlib

VDEV0=$TEST_BASE_DIR/degraded-vdev0
VDEV1=$TEST_BASE_DIR/degraded-vdev1

function cleanup
{
	zinject -c all >/dev/null 2>&1
	destroy_pool $TESTPOOL
	rm -f $VDEV0 $VDEV1
}

#
# Print "<ganged> <total>" block pointer counts at the given level of
# the file, and fail if any of them are not on the expected vdev.
#
# $1 file name in $TESTPOOL/$TESTFS
# $2 level (L0, L1)
# $3 expected vdev id, or "any"
#
function count_gang_bps
{
	get_blocks_filter $TESTPOOL/$TESTFS $1 $2 | awk -v vd=$3 '
	    { split($3, a, ":"); t++; if ($3 ~ /G$/) g++
	      if (vd != "any" && a[1] != vd) bad++ }
	    END { print g + 0, t + 0, bad + 0 }'
}

function write_and_check
{
	typeset name=$1
	typeset vd=$2

	log_must dd if=/dev/urandom of=$mntpnt/$name bs=128k count=256
	log_must zpool sync $TESTPOOL

	typeset l0 l1
	l0=($(count_gang_bps $name L0 $vd))
	l1=($(count_gang_bps $name L1 $vd))
	log_note "$name: L0 ${l0[0]}/${l0[1]} ganged, L1 ${l1[0]}/${l1[1]}" \
	    "ganged, ${l0[2]} L0 blocks not on vdev $vd"
	(( ${l0[1]} == 256 )) || log_fail "expected 256 L0 blocks"
	(( ${l0[0]} == 0 )) || log_fail "${l0[0]} L0 blocks are ganged"
	(( ${l1[0]} == 0 )) || log_fail "${l1[0]} L1 blocks are ganged"
	(( ${l0[2]} == 0 )) || log_fail "${l0[2]} L0 blocks not on vdev $vd"
}

log_assert "A degraded single-disk vdev does not force gang blocks"
log_onexit cleanup

log_must truncate -s $MINVDEVSIZE $VDEV0 $VDEV1

# One top-level vdev: it has to be used, and without ganging.
log_must zpool create -f -o ashift=12 $TESTPOOL $VDEV0
log_must zfs create -o recordsize=128k -o compression=off $TESTPOOL/$TESTFS
mntpnt=$(get_prop mountpoint $TESTPOOL/$TESTFS)
log_must zinject -d $VDEV0 -A degrade $TESTPOOL
log_must check_state $TESTPOOL $VDEV0 DEGRADED
write_and_check file1 any
destroy_pool $TESTPOOL

# Two top-level vdevs: the healthy one is preferred.
log_must zpool create -f -o ashift=12 $TESTPOOL $VDEV0 $VDEV1
log_must zfs create -o recordsize=128k -o compression=off $TESTPOOL/$TESTFS
mntpnt=$(get_prop mountpoint $TESTPOOL/$TESTFS)
log_must zinject -d $VDEV0 -A degrade $TESTPOOL
log_must check_state $TESTPOOL $VDEV0 DEGRADED
write_and_check file2 1

log_pass "A degraded single-disk vdev does not force gang blocks"
