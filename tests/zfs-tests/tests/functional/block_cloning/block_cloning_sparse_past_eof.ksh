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

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/block_cloning/block_cloning.kshlib

#
# DESCRIPTION:
#	Cloning a sparse file takes time in proportion to its data,
#	not its size. The clone keeps its size across ZIL replay.
#
# STRATEGY:
#	1. Create a pool with a separate log device.
#	2. Create a 2^62-byte file whose only data is its last block,
#	   an empty file of the same size, a 1 MiB data file written
#	   with O_SYNC so the intent log exists, and an empty 1 MiB
#	   file. In a second dataset with 16 KiB records, create a
#	   sparse 1 GiB file and a 16 KiB data file.
#	3. Freeze the pool.
#	4. Clone each large file within 60 seconds, clone the empty
#	   clone again, and clone the empty 1 MiB file over the data.
#	   Clone the sparse file from the 16 KiB dataset, then clone
#	   the data block into the result. Replay applies that only
#	   if the first clone kept the source block size.
#	5. Check sizes and contents.
#	6. Export and import the pool to replay the intent log, and
#	   check again.
#	7. Clone another 16 KiB block into the replayed file, and
#	   check that it was cloned, not copied.
#

verify_runnable "global"

export VDIR=$TEST_BASE_DIR/disk-bclone
export VDEV="$VDIR/a $VDIR/b $VDIR/c"
export LDEV="$VDIR/e $VDIR/f"
log_must rm -rf $VDIR
log_must mkdir -p $VDIR
log_must truncate -s $MINVDEVSIZE $VDEV $LDEV

claim="Cloning a sparse file skips holes past the destination EOF"

log_assert $claim

function cleanup
{
	datasetexists $TESTPOOL && destroy_pool $TESTPOOL
	rm -rf $VDIR
}

log_onexit cleanup

typeset -li bs=131072
typeset -li big=$((1 << 62))
typeset -li last=$((big / bs - 1))
typeset fs=/$TESTPOOL/$TESTFS
typeset fs16=/$TESTPOOL/$TESTFS.16k
typeset -li gb=$((1 << 30))

function check_files
{
	log_must [ $(stat_size $fs/clone_tail) -eq $big ]
	log_must [ $(stat_size $fs/clone_empty) -eq $big ]
	log_must [ $(stat_size $fs/clone_empty2) -eq $big ]
	log_must dd if=$fs/tail of=$VDIR/want bs=$bs skip=$last \
	    count=1
	log_must dd if=$fs/clone_tail of=$VDIR/got bs=$bs skip=$last \
	    count=1
	log_must cmp $VDIR/want $VDIR/got
	log_must have_same_content $fs/hole $fs/data
	log_must [ $(stat_size $fs/clone16) -eq $gb ]
	log_must dd if=$fs/clone16 of=$VDIR/got16 bs=16384 count=1
	log_must cmp $fs16/blk $VDIR/got16
}

#
# 1. Create a pool with a separate log device.
#
log_must zpool create -o feature@block_cloning=enabled $TESTPOOL \
    $VDEV log mirror $LDEV
log_must zfs create -o recordsize=128k $TESTPOOL/$TESTFS
log_must zfs create -o recordsize=16k $TESTPOOL/$TESTFS.16k

#
# 2. Create the source files.
#
log_must dd if=/dev/urandom of=$fs/tail bs=$bs count=1 seek=$last
log_must truncate -s $big $fs/empty
log_must dd if=/dev/urandom of=$fs/data bs=$bs count=8 oflag=sync
log_must truncate -s $((8 * bs)) $fs/hole
log_must truncate -s $gb $fs16/src
log_must dd if=/dev/urandom of=$fs16/blk bs=16384 count=1
sync_pool $TESTPOOL

#
# 3. Freeze the pool.
#
log_must zpool freeze $TESTPOOL

#
# 4. Clone.
#
log_must timeout 60 clonefile -c $fs/tail $fs/clone_tail
log_must timeout 60 clonefile -c $fs/empty $fs/clone_empty
log_must timeout 60 clonefile -c $fs/clone_empty $fs/clone_empty2
log_must clonefile -r $fs/hole $fs/data 0 0 $((8 * bs))
log_must timeout 60 clonefile -f $fs16/src $fs/clone16
log_must clonefile -f $fs16/blk $fs/clone16 0 0 16384

#
# 5. Check sizes and contents.
#
check_files

#
# 6. Replay the intent log and check again.
#
log_must zfs unmount $TESTPOOL/$TESTFS.16k
log_must zfs unmount $TESTPOOL/$TESTFS
log_must zpool export $TESTPOOL
log_must zpool import -f -d $VDIR $TESTPOOL
check_files

#
# 7. Replay must keep the source's block size on clone16, or a later
#    clone of a 16 KiB block into it fails and copy_file_range falls
#    back to copying. The block must be shared, not copied.
#
log_must dd if=/dev/urandom of=$fs16/blk2 bs=16384 count=1
log_must clonefile -f $fs16/blk2 $fs/clone16 0 0 16384
sync_pool $TESTPOOL
typeset blocks=$(get_same_blocks $TESTPOOL/$TESTFS.16k blk2 \
    $TESTPOOL/$TESTFS clone16)
log_must [ "$blocks" = "0" ]

log_pass $claim
