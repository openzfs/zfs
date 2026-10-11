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
# 'zpool status -v' must not hang when resolving an entry of the in-core
# error list needs a read that fails.
#
# With feature@head_errlog, spa_get_errlog() looks up the birth txg of
# each in-core error list entry, which reads the dnode and indirect
# blocks of the damaged object.  A failed read posts an ereport and logs
# the error, both of which take spa_errlist_lock, so the list must not
# be processed with that lock held.
#
# STRATEGY:
# 1. Create a pool and a file with an L1 indirect block.
# 2. Keep the error lists in core (long txg timeout).
# 3. Inject a checksum error into a data block of the file and read it,
#    so that the block is added to the in-core error list.
# 4. Evict the file's dbufs, and inject a checksum error into its L1
#    block, which 'zpool status -v' has to read to resolve the entry.
# 5. Verify that 'zpool status -v' completes.
#

verify_runnable "global"

TESTPOOL2=testpool_errlog
VDEV=$TEST_BASE_DIR/vdev_errlog
STATUS_PID=

function cleanup
{
	if [[ -n "$STATUS_PID" ]] && kill -0 $STATUS_PID 2>/dev/null; then
		# The pool is wedged, don't hang the cleanup as well.
		log_note "zpool status -v is still running, pool left as is"
		return
	fi
	zinject -c all >/dev/null 2>&1
	restore_tunable TXG_TIMEOUT
	restore_tunable DBUF_CACHE_MAX_BYTES
	restore_tunable DBUF_METADATA_CACHE_MAX_BYTES
	destroy_pool $TESTPOOL2
	rm -f $VDEV
}

log_assert "'zpool status -v' completes when resolving an error fails"
log_onexit cleanup

log_must save_tunable TXG_TIMEOUT
log_must save_tunable DBUF_CACHE_MAX_BYTES
log_must save_tunable DBUF_METADATA_CACHE_MAX_BYTES

log_must rm -f $VDEV
log_must truncate -s $MINVDEVSIZE $VDEV
log_must zpool create -O compression=off -O atime=off -O recordsize=128k \
    $TESTPOOL2 $VDEV
log_must test "$(get_pool_prop feature@head_errlog $TESTPOOL2)" = "active"
log_must zfs create $TESTPOOL2/fs
mntpnt=$(get_prop mountpoint $TESTPOOL2/fs)
log_must dd if=/dev/urandom of=$mntpnt/file bs=128k count=8
log_must zpool sync $TESTPOOL2
objset=$(printf "%x" $(get_prop objsetid $TESTPOOL2/fs))
object=$(printf "%x" $(ls -i $mntpnt/file | awk '{print $1}'))

# Drop the cached data of the file.
log_must zfs unmount $TESTPOOL2/fs
log_must zfs mount $TESTPOOL2/fs

# No txg syncs from here on, so the errors stay in the in-core lists.
log_must set_tunable64 TXG_TIMEOUT 600

log_must zinject -b $objset:$object:0:0 -e checksum -a $TESTPOOL2
log_mustnot dd if=$mntpnt/file of=/dev/null bs=128k count=1
log_must zinject -c all

# Evict the dbufs of the file, including its L1 block, without syncing.
log_must set_tunable64 DBUF_CACHE_MAX_BYTES 4096
log_must set_tunable64 DBUF_METADATA_CACHE_MAX_BYTES 4096
log_must sleep 3
log_must restore_tunable DBUF_CACHE_MAX_BYTES
log_must restore_tunable DBUF_METADATA_CACHE_MAX_BYTES

log_must zinject -b $objset:$object:1:0 -e checksum -a $TESTPOOL2

zpool status -v $TESTPOOL2 > $TEST_BASE_DIR/status_errlog.out 2>&1 &
STATUS_PID=$!
typeset -i i=0
while ((i < 30)) && kill -0 $STATUS_PID 2>/dev/null; do
	sleep 1
	((i += 1))
done
if kill -0 $STATUS_PID 2>/dev/null; then
	log_fail "'zpool status -v' hung resolving the error list"
fi
log_must wait $STATUS_PID
STATUS_PID=
log_must grep -q "Permanent errors" $TEST_BASE_DIR/status_errlog.out
rm -f $TEST_BASE_DIR/status_errlog.out

log_pass "'zpool status -v' completes when resolving an error fails"
