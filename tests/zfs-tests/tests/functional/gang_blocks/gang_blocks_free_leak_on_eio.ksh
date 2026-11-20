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
# Verify that freeing a gang block whose gang header copies are all
# unreadable succeeds and leaks the gang children when zfs_free_leak_on_eio
# is enabled, rather than panicking or suspending the pool.
#
# Strategy:
# 1. Create a pool on a file vdev and force 128K blocks to gang.
# 2. Write two 128K files with copies=1, each having two gang header copies.
# 3. Export the pool and overwrite both gang header copies of each file.
# 4. Import the pool and enable FREE_LEAK_ON_EIO.
# 5. Remove the first file (synchronous free via spa_sync_frees / free_bpobj)
#    and destroy the dataset containing the second file (asynchronous destroy
#    via dsl_process_async_destroys / bptree).
# 6. Verify that the frees complete and the pool remains ONLINE.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/gang_blocks/gang_blocks.kshlib

verify_runnable "global"

typeset vdev=$TEST_BASE_DIR/gang_blocks_free_leak_on_eio
typeset -i sector_size=4096

function cleanup2
{
	restore_tunable FREE_LEAK_ON_EIO
	cleanup
	rm -f $vdev
}

#
# Print the sector of the vdev at which a copy, given as a DVA in zdb's
# vdev:offset:asize form, starts.  The front labels and the boot block
# take the first 4 MiB of the vdev.
#
function dva_sector # dva
{
	typeset offset=$(echo $1 | awk -F: '{print $2}')

	echo $(( (0x$offset + (4 << 20)) / sector_size ))
}

#
# Overwrite a sector with random data.
#
function damage_sector # sector
{
	log_must dd if=/dev/urandom of=$vdev bs=$sector_size seek=$1 \
	    count=1 conv=notrunc
}

log_assert "Freeing a gang block with a corrupt gang header leaks space when" \
    "zfs_free_leak_on_eio is set."

preamble
save_tunable FREE_LEAK_ON_EIO
log_onexit cleanup2

log_must truncate -s 256m $vdev
log_must zpool create -o ashift=12 $TESTPOOL $vdev
log_must zfs create $TESTPOOL/$TESTFS

set_tunable64 METASLAB_FORCE_GANGING 102400
set_tunable32 METASLAB_FORCE_GANGING_PCT 100
mntpnt=$(get_prop mountpoint $TESTPOOL/$TESTFS)
log_must dd if=/dev/urandom of=$mntpnt/file1 bs=128k count=1
log_must dd if=/dev/urandom of=$mntpnt/file2 bs=128k count=1
sync_pool $TESTPOOL
restore_tunable METASLAB_FORCE_GANGING
restore_tunable METASLAB_FORCE_GANGING_PCT

typeset -a sectors
for f in file1 file2; do
	blk=$(get_first_block $TESTPOOL/$TESTFS $f)
	hdr0=$(echo "$blk" | awk '{print $3}')
	hdr1=$(echo "$blk" | awk '{print $4}')
	check_is_gang_dva $hdr0
	check_is_gang_dva $hdr1
	sectors+=($(dva_sector $hdr0) $(dva_sector $hdr1))
done

log_must zpool export $TESTPOOL
for s in "${sectors[@]}"; do
	damage_sector $s
done
log_must zpool import -d $vdev $TESTPOOL

set_tunable32 FREE_LEAK_ON_EIO 1

# Test synchronous/deferred free path (spa_sync_frees / dp_free_bpobj)
log_must rm $mntpnt/file1
sync_pool $TESTPOOL

# Test asynchronous dataset destroy path (dsl_process_async_destroys / bptree)
log_must zfs destroy $TESTPOOL/$TESTFS
sync_pool $TESTPOOL
log_must zpool wait -t free $TESTPOOL

[[ $(get_pool_prop health $TESTPOOL) == "ONLINE" ]] || \
    log_fail "Pool did not remain ONLINE after freeing corrupt gang headers"

log_pass "Corrupt gang headers were leaked without suspending the pool."
