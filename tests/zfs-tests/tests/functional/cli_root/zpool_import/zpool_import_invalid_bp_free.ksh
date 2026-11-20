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

#
# DESCRIPTION:
# A block pointer whose DVA fails zfs_blkptr_verify() but which is
# covered by a valid checksum in its parent (e.g. a bit flip in memory
# before the parent was written) must not be passed to metaslab_free()
# when the block is freed.  With zfs_recover set the block must be leaked
# and the pool must stay usable.
#
# The pre-packaged pool has two such block pointers, both for block 3 of
# 'file' (4k records), with bit 60 of DVA[0]'s offset set: one in
# invalid_bp/fs1 and one in invalid_bp/fs2.
#
# STRATEGY:
# 1. Import the pool read-only; the damaged block reads as an error.
# 2. Set zfs_recover and import the pool read-write.
# 3. Overwrite the damaged block of fs1/file (free via zio_free()).
# 4. Destroy fs2 (free via async destroy and zio_free_sync()).
# 5. Verify the pool is healthy, writable and scrubs clean.
# 6. Verify zdb reports exactly the two damaged blocks as leaked.
#

verify_runnable "global"

POOL_NAME=invalid_bp
POOL_FILE=invalid_bp.dat
IMG_DIR=$TEST_BASE_DIR/invalid_bp_free.$$
ALTROOT=$TEST_BASE_DIR/invalid_bp_free_root.$$
ZDB_OUT=$TEST_BASE_DIR/invalid_bp_free_zdb.$$

function cleanup
{
	poolexists $POOL_NAME && destroy_pool $POOL_NAME
	rm -rf $IMG_DIR $ALTROOT $ZDB_OUT
	log_must set_tunable32 RECOVER $saved_recover
}

log_assert "Freeing an invalid block pointer leaks it with zfs_recover set"
saved_recover=$(get_tunable RECOVER)
log_onexit cleanup

log_must mkdir -p $IMG_DIR $ALTROOT
log_must eval "bzcat <$STF_SUITE/tests/functional/cli_root/zpool_import/blockfiles/$POOL_FILE.bz2 >$IMG_DIR/$POOL_FILE"

# 1. Read-only import works; the damaged block cannot be read.
log_must zpool import -d $IMG_DIR -o readonly=on -R $ALTROOT $POOL_NAME
log_must dd if=$ALTROOT/$POOL_NAME/fs1/file of=/dev/null bs=4k count=3
log_mustnot dd if=$ALTROOT/$POOL_NAME/fs1/file of=/dev/null bs=4k skip=3 \
    count=1
log_must zpool export $POOL_NAME

# 2. Read-write import with zfs_recover set.
log_must set_tunable32 RECOVER 1
log_must zpool import -d $IMG_DIR -R $ALTROOT $POOL_NAME

# 3. Overwriting the block frees the old, invalid, block pointer.
log_must dd if=/dev/urandom of=$ALTROOT/$POOL_NAME/fs1/file bs=4k \
    count=1 seek=3 conv=notrunc
sync_pool $POOL_NAME
log_must dd if=$ALTROOT/$POOL_NAME/fs1/file of=/dev/null bs=4k

# 4. Destroying the dataset frees it from the async destroy path.
log_must zfs destroy $POOL_NAME/fs2
log_must zpool wait -t free $POOL_NAME
sync_pool $POOL_NAME

# 5. The pool keeps working.
log_must zfs create $POOL_NAME/fs3
log_must dd if=/dev/urandom of=$ALTROOT/$POOL_NAME/fs3/file bs=128k count=8
sync_pool $POOL_NAME
log_must zpool scrub -w $POOL_NAME
log_must check_state $POOL_NAME "" "ONLINE"
log_must eval "zpool status $POOL_NAME | grep -q 'with 0 errors'"
log_must eval "zpool status $POOL_NAME | grep -q 'No known data errors'"
log_must zpool export $POOL_NAME

# 6. Both damaged blocks were leaked rather than freed.
zdb -e -p $IMG_DIR -b $POOL_NAME >$ZDB_OUT 2>&1
log_note "$(grep -i leak $ZDB_OUT)"
log_must test "$(grep -c '^leaked space:' $ZDB_OUT)" -eq 2

log_must zpool import -d $IMG_DIR -R $ALTROOT $POOL_NAME

log_pass "Freeing an invalid block pointer leaks it with zfs_recover set"
