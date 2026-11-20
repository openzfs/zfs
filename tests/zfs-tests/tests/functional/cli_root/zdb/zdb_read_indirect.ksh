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
# Copyright (c) 2026 by Oxide Computer Company. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/removal/removal.kshlib

#
# Description:
# zdb -R of a range on an indirect (removed) vdev reads the data when the
# range is mapped, through any further indirect vdevs, and reports the
# range as not mapped, without aborting, when it is not.
#
# Strategy:
# 1. Create a pool on vdev A and write two files, keep and gone.
# 2. Add vdev B and remove A, so that A's mapping points to B.
# 3. Add vdev C and remove B.  While B's removal is paused, remove gone,
#    so that B's mapping does not cover gone's copy on B.
# 4. zdb -R the first block of keep at its original DVA on A: the data
#    must be read through A's and B's mappings from C.
# 5. zdb -R the first block of gone at its original DVA on A: zdb must
#    report the range as not mapped and exit 0.
# 6. Add vdev E and remove it before anything is allocated on it, so
#    that its mapping has no entries.
# 7. zdb -R a range on E: zdb must report the range as not mapped and
#    exit 0.
#

verify_runnable "global"

VDEV_A=$TEST_BASE_DIR/zdb_read_indirect_a
VDEV_B=$TEST_BASE_DIR/zdb_read_indirect_b
VDEV_C=$TEST_BASE_DIR/zdb_read_indirect_c
VDEV_E=$TEST_BASE_DIR/zdb_read_indirect_e
EXPECTED=$TEST_BASE_DIR/zdb_read_indirect.expected
ACTUAL=$TEST_BASE_DIR/zdb_read_indirect.actual
BLKSIZE=131072

function cleanup
{
	log_must set_tunable32 REMOVAL_SUSPEND_PROGRESS 0
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
	rm -f $VDEV_A $VDEV_B $VDEV_C $VDEV_E* $EXPECTED $ACTUAL
}

# Print the DVA of the first L0 block of a file.
function first_l0_dva # file
{
	typeset obj=$(ls -i $1 | awk '{print $1}')

	zdb -ddddddbbbbbb $TESTPOOL/$TESTFS $obj 2>/dev/null | \
	    grep "L0 DVA" | head -n 1 | \
	    sed -Ene 's/^.+DVA\[0\]=<([^>]+)>.*$/\1/p'
}

# zdb -R a range that is not mapped, and check what zdb reports.
function check_not_mapped # vdev:offset:size indirect-vdev-id
{
	typeset out

	out=$(zdb -R $TESTPOOL $1 2>&1)
	typeset rv=$?
	log_note "zdb -R $TESTPOOL $1 exited $rv: $out"
	(( rv == 0 )) || log_fail "zdb -R $TESTPOOL $1 exited $rv"
	[[ "$out" == *"Range not mapped on indirect vdev $2 "* ]] || \
	    log_fail "zdb -R $TESTPOOL $1 did not report the unmapped range"
}

function free_gone
{
	rm -f $TESTDIR/gone || return 1
	sync_pool $TESTPOOL
	sync_pool $TESTPOOL
}

log_assert "zdb -R of an indirect vdev reads mapped ranges and reports" \
    "unmapped ones"
log_onexit cleanup

#
# A's mapping is far too small to be condensed (zfs_condense_min_mapping_bytes),
# so it keeps gone's range.
#
log_must truncate -s $MINVDEVSIZE $VDEV_A
log_must truncate -s $(($MINVDEVSIZE * 4)) $VDEV_B $VDEV_C
default_setup_noexit $VDEV_A
log_must zfs set compression=off $TESTPOOL/$TESTFS
log_must dd if=/dev/urandom of=$TESTDIR/keep bs=$BLKSIZE count=8
log_must dd if=/dev/urandom of=$TESTDIR/gone bs=$BLKSIZE count=8
sync_pool $TESTPOOL true

keep_dva=$(first_l0_dva $TESTDIR/keep)
gone_dva=$(first_l0_dva $TESTDIR/gone)
log_note "keep block 0 is at $keep_dva, gone block 0 is at $gone_dva"
[[ $keep_dva == 0:*:20000 && $gone_dva == 0:*:20000 ]] || \
    log_fail "unexpected DVAs: keep $keep_dva, gone $gone_dva"

log_must zpool add $TESTPOOL $VDEV_B
log_must zpool remove $TESTPOOL $VDEV_A
log_must wait_for_removal $TESTPOOL

log_must zpool add $TESTPOOL $VDEV_C
log_must attempt_during_removal $TESTPOOL $VDEV_B free_gone

# A mapped range on A is read through B's mapping from C.
log_must eval "dd if=$TESTDIR/keep of=$EXPECTED bs=$BLKSIZE count=1"
log_must eval "zdb -R $TESTPOOL $keep_dva:r > $ACTUAL"
log_must cmp $EXPECTED $ACTUAL

# A range on A that B's mapping does not cover.
check_not_mapped $gone_dva 0

#
# Something may be allocated on E before its removal starts; try again
# with a new vdev until a removal copies nothing.
#
typeset -i i
for i in 1 2 3 4 5; do
	log_must truncate -s $MINVDEVSIZE $VDEV_E$i
	log_must zpool add $TESTPOOL $VDEV_E$i
	log_must zpool remove $TESTPOOL $VDEV_E$i
	log_must wait_for_removal $TESTPOOL
	e_id=$(zpool status $TESTPOOL | \
	    sed -n 's/.*Removal of vdev \([0-9]*\) copied 0 in.*/\1/p')
	[[ -n $e_id ]] && break
	log_note "removal of $VDEV_E$i copied data; trying again"
done
[[ -n $e_id ]] || log_fail "could not remove a vdev with nothing allocated"

# Any range on a vdev whose mapping has no entries.
check_not_mapped $e_id:400000:$(printf %x $BLKSIZE) $e_id

log_pass "zdb -R of an indirect vdev reads mapped ranges and reports" \
    "unmapped ones"
