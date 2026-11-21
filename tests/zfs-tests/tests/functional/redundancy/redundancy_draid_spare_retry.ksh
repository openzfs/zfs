#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or https://opensource.org/licenses/CDDL-1.0.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
#

#
# Copyright (c) 2026 by Wasabi Technologies, Inc.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	When a dRAID child has been replaced by a distributed spare while the
#	original device is still attached, a read of a block whose distributed
#	spare copy is damaged must succeed, even if the block has more damage
#	than parity alone can repair.  The retried read compares the copies of
#	the spare vdev and uses the original device's copy.
#
# STRATEGY:
#	1. Create a draid1 pool where every redundancy group spans every disk,
#	   so every block has a column on every disk.
#	2. Write a file with known contents.
#	3. Disable the scrub which normally follows a sequential rebuild.
#	4. Inject read corruption on disk Q and sequentially rebuild an online
#	   disk X to the distributed spare.  A sequential rebuild does not
#	   verify checksums, so the spare's copy of the columns rebuilt from
#	   corrupted reads of Q is silently wrong.  X stays attached as the
#	   other child of the spare vdev, and the data on disk on X and Q is
#	   intact since only reads were corrupted.
#	5. Clear the injection, and export and import the pool so the file is
#	   read from disk.
#	6. Inject read corruption on Q again.  Blocks whose spare copy is
#	   wrong now have two damaged columns, which exceeds the single parity.
#	7. Read the file back and verify its contents.
#	8. Verify the vdev_mirror_stats kstats recorded retry compares which
#	   found differing copies.
#	9. Clear the injection and verify a scrub finds no errors.
#
#	Without the retry compare change step 7 fails with EIO.
#
#	zinject cannot target a distributed spare, which is why its copy is
#	damaged through the rebuild in step 4 instead.
#

verify_runnable "global"

typeset DSPARE="draid1-0-0"
typeset REF_FILE="$TEST_BASE_DIR/draid_spare_retry.ref"
typeset VDEV_PREFIX="$TEST_BASE_DIR/draid_spare_retry.vdev"
typeset VDEV_SIZE=512m
typeset SAVED_REBUILD_SCRUB=""
set -A DISKS

function cleanup
{
	zinject -c all >/dev/null 2>&1
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
	rm -f $REF_FILE ${VDEV_PREFIX}.*
	[[ -n "$SAVED_REBUILD_SCRUB" ]] && \
	    set_tunable32 REBUILD_SCRUB_ENABLED $SAVED_REBUILD_SCRUB
}

#
# Print the value of a vdev_mirror_stats kstat.
#
function mirror_stat # name
{
	typeset name=$1

	if is_linux; then
		awk -v n="$name" '$1 == n { print $3 }' \
		    /proc/spl/kstat/zfs/vdev_mirror_stats
	elif is_freebsd; then
		sysctl -n kstat.zfs.misc.vdev_mirror_stats.$name
	else
		log_unsupported "No vdev_mirror_stats kstat on this platform"
	fi
}

log_onexit cleanup

log_assert "A read succeeds using the original device's copy when the " \
    "distributed spare's copy is damaged beyond parity"

if [[ -z "$(mirror_stat retry_compare_mismatch)" ]]; then
	log_unsupported "Module lacks the retry_compare_mismatch kstat"
fi

# draid1:4d:6c:1s -> 5 active disks, group width 5, every group on every disk
typeset -i i=0
while (( i < 6 )); do
	DISKS[$i]="${VDEV_PREFIX}.$i"
	log_must truncate -s $VDEV_SIZE ${DISKS[$i]}
	(( i += 1 ))
done

log_must zpool create -f -O compression=off -O recordsize=128k \
    -O atime=off $TESTPOOL draid1:4d:6c:1s ${DISKS[@]}

# A larger file means more rebuild I/Os, and so more damaged spare columns.
log_must dd if=/dev/urandom of=$REF_FILE bs=1M count=64
log_must cp $REF_FILE /$TESTPOOL/file
log_must zpool sync $TESTPOOL

# The post-rebuild scrub would repair the spare's copy straight away.
SAVED_REBUILD_SCRUB=$(get_tunable REBUILD_SCRUB_ENABLED)
log_must set_tunable32 REBUILD_SCRUB_ENABLED 0

# X = DISKS[1] is spared, Q = DISKS[2] returns corrupted reads.
log_must zinject -d ${DISKS[2]} -e corrupt -T read -f 100 $TESTPOOL
log_must zpool replace -s -w $TESTPOOL ${DISKS[1]} $DSPARE
log_must zinject -c all
log_must check_vdev_state $TESTPOOL $DSPARE "ONLINE"
log_must check_vdev_state $TESTPOOL ${DISKS[1]} "ONLINE"
log_must eval "zpool status $TESTPOOL | grep -q 'spare-[0-9]'"

#
# Drop cached data so the file is read from disk.  Nothing must read the
# file before the injection below: a normal read would repair the spare.
#
log_must zpool export $TESTPOOL
log_must zpool import -d $TEST_BASE_DIR $TESTPOOL
log_must zinject -d ${DISKS[2]} -e corrupt -T read -f 100 $TESTPOOL

typeset -i before_cmp=$(mirror_stat retry_compare)
typeset -i before_mis=$(mirror_stat retry_compare_mismatch)

log_must cmp $REF_FILE /$TESTPOOL/file

typeset -i after_cmp=$(mirror_stat retry_compare)
typeset -i after_mis=$(mirror_stat retry_compare_mismatch)
log_note "retry compares: $((after_cmp - before_cmp)), " \
    "mismatches: $((after_mis - before_mis))"

if (( after_mis <= before_mis )); then
	log_fail "No retry compare mismatches were recorded"
fi
log_must check_pool_status $TESTPOOL "errors" "No known data errors"

# The damage was injected on read only, so the on-disk data is intact.
log_must zinject -c all
log_must zpool clear $TESTPOOL
log_must zpool scrub -w $TESTPOOL
log_must check_pool_status $TESTPOOL "scan" "with 0 errors"
log_must check_pool_status $TESTPOOL "errors" "No known data errors"
log_must cmp $REF_FILE /$TESTPOOL/file

log_pass "A read succeeds using the original device's copy when the " \
    "distributed spare's copy is damaged beyond parity"
