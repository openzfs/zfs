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

#
# Description:
#
# zdb -AA sets zfs_recover: loading a space map whose entries overlap must
# then report each overlap and go on, where plain zdb stops.
#
# Strategy:
#
# 1. Create a pool, write a file of one 128K block and export the pool.
# 2. With zhack metaslab append, add four entries to the space map of the
#    metaslab that holds the block: free the block twice, then allocate it
#    twice.  The second free and the second allocation each overlap the
#    state the first one left, and together the four leave the block
#    allocated, as the space map's count of allocated space still says.
# 3. Verify that zdb -mmm, which loads every metaslab, fails.
# 4. Verify that zdb -AA -mmm succeeds and reports both overlaps.
#

. "$STF_SUITE"/include/libtest.shlib

verify_runnable "global"

typeset dir=$TEST_BASE_DIR/zdb_space_map_overlap
typeset file=/$TESTPOOL/file

function cleanup
{
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
	rm -rf $dir
}

log_onexit cleanup
log_assert "zdb -AA loads a space map whose entries overlap"

log_must mkdir $dir
log_must truncate -s $MINVDEVSIZE $dir/vdev
log_must zpool create -o ashift=12 -O compression=off -O recordsize=128k \
    $TESTPOOL $dir/vdev
log_must dd if=/dev/urandom of=$file bs=128k count=1
sync_pool $TESTPOOL

# The DVA of the file's only block, as <vdev>:<offset>:<asize> in hex.
typeset dva id offset asize
dva=$(zdb -Ovv $TESTPOOL file |
    awk '/ L0 / { n++; dva = $3 } END { if (n == 1) print dva }')
log_note "$file: $dva"
IFS=: read -r id offset asize <<< "$dva"
[[ $id == 0 && $asize == 20000 ]] || log_fail "unexpected DVA '$dva'"

log_must_busy zpool export $TESTPOOL
log_must zhack -d $dir metaslab append $TESTPOOL 0 free 0x$offset 0x$asize \
    free 0x$offset 0x$asize alloc 0x$offset 0x$asize alloc 0x$offset 0x$asize

# Without -AA the first overlap aborts zdb; keep it from dumping core.
(ulimit -c 0; zdb -e -p $dir -mmm $TESTPOOL) > $dir/zdb.out 2>&1 &&
    log_fail "zdb -mmm loaded overlapping space map entries"
log_must eval "zdb -AA -e -p $dir -mmm $TESTPOOL > $dir/zdb-AA.out 2>&1"
log_must grep -q "adding segment .* overlapping" $dir/zdb-AA.out
log_must grep -Eq "removing (nonexistent )?segment" $dir/zdb-AA.out

log_pass "zdb -AA loads a space map whose entries overlap"
