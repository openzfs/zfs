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
#	An error in the block of a claimed intent log write record is logged
#	with the log's bookmark level. Listing it must not look that level up
#	in the file's block tree, and neither listing it nor an error scrub,
#	which cannot find the block there either, may drop it.
#
# STRATEGY:
#	1. Overwrite part of an existing file synchronously in a frozen pool,
#	   with its block written outside the log.
#	2. Import without mounting, which claims the log, then damage the
#	   block on disk.
#	3. Mount the file system, whose replay fails to read the block, and
#	   list the pool's errors twice, then again after an error scrub.
#

verify_runnable "global"

function cleanup
{
	destroy_pool $TESTPOOL1
	rm -rf $workdir
}

log_assert "Errors in blocks of claimed log records are listed and kept"
workdir=$(mktemp -d $TEST_BASE_DIR/zpool_status_claimed_log.XXXXXX) ||
    log_fail "cannot create test directory"
log_onexit cleanup

log_must truncate -s 512M $workdir/disk-0
log_must zpool create -f $TESTPOOL1 $workdir/disk-0
log_must zfs create -o compression=off -o recordsize=128k \
    -o logbias=throughput $TESTPOOL1/$TESTFS
mntpnt=$(get_prop mountpoint $TESTPOOL1/$TESTFS)
log_must dd if=/dev/urandom of=$mntpnt/file bs=128k count=2
# Records written after the freeze need a ZIL header already on disk.
log_must dd if=/dev/zero of=$mntpnt/sync conv=fdatasync,fsync bs=1 count=1
sync_pool $TESTPOOL1
log_must zpool freeze $TESTPOOL1
log_must dd if=/dev/urandom of=$mntpnt/file bs=128k count=1 conv=notrunc,fsync
log_must zpool export $TESTPOOL1
# Frozen labels need -f. Without mounting, the log stays claimed.
log_must zpool import -f -N -d $workdir $TESTPOOL1

log_must eval "zdb -ivvvvv $TESTPOOL1/$TESTFS >$workdir/zil"
# The block pointer of the write record follows its "has blkptr" line.
# shellcheck disable=SC2016 # awk field references
awk '/has blkptr/ { want = 1; next }
    want { for (i = 1; i <= NF; i++) if ($i ~ /^DVA/) {
	split($i, dva, /[<:>]/); print dva[3], dva[4]; exit } }' \
    $workdir/zil >$workdir/dva || log_fail "cannot parse the write's DVA"
read -r offset asize <$workdir/dva
[[ -n "$offset" ]] || log_fail "no claimed write record with a block"
log_must zpool export $TESTPOOL1
log_must dd if=/dev/urandom of=$workdir/disk-0 bs=512 conv=notrunc \
    seek=$(( (0x$offset + 4194304) / 512 )) count=$(( 0x$asize / 512 ))
log_must zpool import -N -d $workdir $TESTPOOL1

# Replay reads the damaged block and logs the error for the file.
zfs mount $TESTPOOL1/$TESTFS
for check in listed "listed again" "kept by an error scrub"; do
	[[ $check == kept* ]] && log_must zpool scrub -e -w $TESTPOOL1
	log_must eval "zpool status -v $TESTPOOL1 >$workdir/status"
	grep -q "$TESTFS.*/file" $workdir/status ||
	    log_fail "the file's error was not $check: $(cat $workdir/status)"
	sync_pool $TESTPOOL1
done

log_pass "Errors in blocks of claimed log records were listed and kept"
