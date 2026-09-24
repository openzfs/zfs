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
#	A scrub reports damage to a block written through the intent log,
#	while the log is claimed but not yet replayed, and the error log keeps
#	the error while the log still references the block.
#
# STRATEGY:
#	1. Freeze a pool, write a file synchronously with logbias=throughput
#	   so that its block is written outside the log, and export.
#	2. Import without mounting, which claims the log and reads the block.
#	3. Damage the block on disk, scrub, and require a checksum error.
#	4. Require the error to be listed twice, after an error scrub where
#	   supported, and after an import which leaves the log unreplayed.
#	5. Repeat with and without the head_errlog feature.
#

verify_runnable "global"

function cleanup
{
	destroy_pool $TESTPOOL1
	rm -rf $workdir
}

function check_listed # <what>
{
	log_must eval "zpool status -v $TESTPOOL1 >$workdir/status"
	grep -A10 'following files:' $workdir/status |
	    grep -q "$TESTPOOL1/$TESTFS" ||
	    log_fail "the error was not $1: $(cat $workdir/status)"
	sync_pool $TESTPOOL1
}

log_assert "Scrub reports damage to a block of a claimed log record"
workdir=$(mktemp -d $TEST_BASE_DIR/zpool_scrub_claimed_log.XXXXXX) ||
    log_fail "cannot create test directory"
log_onexit cleanup

for errlog in enabled disabled; do
	log_must truncate -s 512M $workdir/disk-0
	log_must zpool create -f -o feature@head_errlog=$errlog \
	    $TESTPOOL1 $workdir/disk-0
	log_must zfs create -o compression=off -o recordsize=128k \
	    -o logbias=throughput $TESTPOOL1/$TESTFS
	mntpnt=$(get_prop mountpoint $TESTPOOL1/$TESTFS)
	# Records written after the freeze need a ZIL header on disk.
	log_must dd if=/dev/zero of=$mntpnt/sync conv=fdatasync,fsync \
	    bs=1 count=1
	log_must zpool freeze $TESTPOOL1
	log_must dd if=/dev/urandom of=$mntpnt/file bs=128k count=1 \
	    conv=fsync
	log_must zpool export $TESTPOOL1
	# Frozen labels need -f. Without mounting, the log stays claimed.
	log_must zpool import -f -N -d $workdir $TESTPOOL1

	log_must eval "zdb -ivvvvv $TESTPOOL1/$TESTFS >$workdir/zil"
	# The block pointer of the write record follows its "has blkptr".
	# shellcheck disable=SC2016 # awk field references
	awk '/has blkptr/ { want = 1; next }
	    want { for (i = 1; i <= NF; i++) if ($i ~ /^DVA/) {
		split($i, dva, /[<:>]/); print dva[3], dva[4]; exit } }' \
	    $workdir/zil >$workdir/dva ||
	    log_fail "cannot parse the write's DVA"
	read -r offset asize <$workdir/dva
	[[ -n "$offset" ]] || log_fail "no claimed write record with a block"
	log_must dd if=/dev/urandom of=$workdir/disk-0 bs=512 conv=notrunc \
	    seek=$(( (0x$offset + 4194304) / 512 )) \
	    count=$(( 0x$asize / 512 ))

	log_must zpool scrub -w $TESTPOOL1
	log_must check_pool_status $TESTPOOL1 "scan" \
	    "with [1-9][0-9]* errors" true
	check_listed "listed"
	check_listed "listed again"
	# Error scrubs require head_errlog.
	if [[ $errlog == enabled ]]; then
		log_must zpool scrub -e -w $TESTPOOL1
		check_listed "kept by an error scrub"
	fi
	log_must zpool export $TESTPOOL1
	log_must zpool import -N -d $workdir $TESTPOOL1
	check_listed "kept after an import"
	log_must zpool destroy $TESTPOOL1
done

log_pass "Scrub reported damage to a block of a claimed log record"
