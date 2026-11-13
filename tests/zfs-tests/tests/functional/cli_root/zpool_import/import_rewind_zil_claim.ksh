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
. $STF_SUITE/tests/functional/cli_root/zpool_import/zpool_import.kshlib

#
# DESCRIPTION:
# A writable 'zpool import -T <txg>' must claim the ZIL that was live at that
# txg relative to that txg, even though newer uberblocks are still on disk.
#
# STRATEGY:
# 1. Create a pool and run a synchronous writer, so the ZIL is never empty.
# 2. Freeze the pool while the writer runs, so the log keeps growing but
#    nothing after the freeze txg reaches the disk: the log blocks written
#    while the last synced txg was open are born after the txg before it,
#    and none of that txg's blocks can have been reused.
# 3. Stop the writer, export, and import the pool read-write at the txg
#    before the newest uberblock.
# 4. Export and import it again, and check it with a scrub and zdb.
#

verify_runnable "global"

typeset pool=rewind_zil_claim
typeset dir=$TEST_BASE_DIR/rewind_zil_claim

function cleanup
{
	poolexists $pool && destroy_pool $pool
	rm -rf $dir
}

log_onexit cleanup

log_assert "zpool import -T claims the ZIL relative to the imported txg"

log_must rm -rf $dir
log_must mkdir -p $dir
log_must truncate -s $MINVDEVSIZE $dir/vdev
log_must zpool create $pool $dir/vdev
log_must zfs create -o sync=always -o recordsize=4k $pool/fs
typeset mntpnt=$(get_prop mountpoint $pool/fs)

(
	typeset -i i=0
	while [[ ! -f $dir/stop ]]; do
		dd if=/dev/urandom of=$mntpnt/f$(( i % 50 )) bs=4k count=8 \
		    oflag=sync conv=notrunc 2>/dev/null
		(( i += 1 ))
	done
) &
typeset writer=$!
sleep 2
log_must zpool freeze $pool
sleep 2
log_must touch $dir/stop
log_must wait $writer
typeset -i txg=$(get_last_txg_synced $pool)
log_must zpool export $pool
(( txg > 0 )) || log_fail "no synced txg found"
((txg -= 1))
log_note "importing at txg $txg"

log_must zpool import -d $dir -T $txg $pool
log_must zpool export $pool
log_must zpool import -d $dir $pool
log_must zpool scrub -w $pool
log_must check_pool_status $pool "errors" "No known data errors"
log_must zpool export $pool
log_must zdb -e -p $dir -b $pool

log_pass "zpool import -T claims the ZIL relative to the imported txg"
