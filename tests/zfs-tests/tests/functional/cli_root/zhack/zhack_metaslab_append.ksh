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
# A space map entry that allocates a range only part of which is free
# must leave none of the range allocatable when the pool is imported with
# zfs_recover set, or the free part is written over.
#
# Strategy:
#
# 1. Create a pool, write a file of one 128K block and export the pool.
# 2. With zhack metaslab append, add three entries to the space map of the
#    metaslab that holds the block: free the block, allocate its first
#    sector, then allocate the whole block again.  Only part of the last
#    entry is free when it is read.
# 3. Import the pool with zfs_recover set.
# 4. Initialize the pool, which writes over all of its free space.
# 5. Scrub the pool and verify that the file is unchanged.
#

. "$STF_SUITE"/include/libtest.shlib

verify_runnable "global"

typeset dir=$TEST_BASE_DIR/zhack_metaslab_append
typeset file=/$TESTPOOL/file

function cleanup
{
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
	log_must restore_tunable RECOVER
	rm -rf $dir
}

log_onexit cleanup
log_assert "zfs_recover keeps a partly free space map allocation allocated"

log_must save_tunable RECOVER

log_must mkdir $dir
log_must truncate -s $MINVDEVSIZE $dir/vdev
log_must zpool create -o ashift=12 -O compression=off -O recordsize=128k \
    $TESTPOOL $dir/vdev
log_must dd if=/dev/urandom of=$file bs=128k count=1
sync_pool $TESTPOOL
typeset sum dva id offset asize
sum=$(xxh128digest $file)

# The DVA of the file's only block, as <vdev>:<offset>:<asize> in hex.
dva=$(zdb -Ovv $TESTPOOL file |
    awk '/ L0 / { n++; dva = $3 } END { if (n == 1) print dva }')
log_note "$file: $dva"
IFS=: read -r id offset asize <<< "$dva"
[[ $id == 0 && $asize == 20000 ]] || log_fail "unexpected DVA '$dva'"

log_must_busy zpool export $TESTPOOL
log_must zhack -d $dir metaslab append $TESTPOOL 0 free 0x$offset 0x$asize \
    alloc 0x$offset 0x1000 alloc 0x$offset 0x$asize

log_must set_tunable32 RECOVER 1
log_must zpool import -d $dir $TESTPOOL

log_must zpool initialize -w $TESTPOOL
verify_pool $TESTPOOL
[[ $(xxh128digest $file) == "$sum" ]] || log_fail "$file has changed"

log_pass "zfs_recover keeps a partly free space map allocation allocated"
