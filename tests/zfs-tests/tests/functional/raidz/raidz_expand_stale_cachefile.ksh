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
#	A pool can be imported from a cachefile written while a RAID-Z
#	expansion was in progress, once the expansion has completed.
#	The cachefile is rewritten only some txgs after the completion
#	reaches the labels, so a crash in between leaves such a cachefile.
#
# STRATEGY:
#	1. Create a raidz pool with a cachefile and write data to it.
#	2. Attach a disk with the reflow paused, and copy the cachefile,
#	   which now says that the vdev is expanding.
#	3. Let the expansion complete, write more data, and export.  The
#	   labels no longer say that the vdev is expanding.
#	4. Import from the copy, which must not need the fallback to a
#	   device scan that zpool import -c takes when the cachefile cannot
#	   import the pool, and must take the layout from the labels.
#	5. Verify the data, that the cachefile was rewritten, and that a
#	   scrub finds no errors.
#

verify_runnable "global"

typeset -r devs=4
typeset -r dev_size_mb=128
typeset -r cachefile=$TEST_BASE_DIR/stale_cachefile.cache
typeset -r stale=$TEST_BASE_DIR/stale_cachefile.expanding
typeset -r out=$TEST_BASE_DIR/stale_cachefile.out
typeset -r sums=$TEST_BASE_DIR/stale_cachefile.sums
typeset -r msg="spa_load($TESTPOOL, config untrusted): vdev 0: using the \
RAID-Z layout of its label"

typeset -a disks

original_scrub_after_expand=$(get_tunable SCRUB_AFTER_EXPAND)
original_max_reflow_bytes=$(get_tunable RAIDZ_EXPAND_MAX_REFLOW_BYTES)
original_dbgmsg_enable=$(get_tunable DBGMSG_ENABLE)

function cleanup
{
	poolexists "$TESTPOOL" && log_must_busy zpool destroy "$TESTPOOL"

	for i in {0..$devs}; do
		log_must rm -f "$TEST_BASE_DIR/dev-$i"
	done
	log_must rm -f $cachefile $stale $out $sums

	log_must set_tunable64 RAIDZ_EXPAND_MAX_REFLOW_BYTES \
	    $original_max_reflow_bytes
	log_must set_tunable32 SCRUB_AFTER_EXPAND $original_scrub_after_expand
	log_must set_tunable32 DBGMSG_ENABLE $original_dbgmsg_enable
}

log_onexit cleanup

log_must set_tunable32 SCRUB_AFTER_EXPAND 0
log_must set_tunable32 DBGMSG_ENABLE 1

for i in {0..$devs}; do
	device=$TEST_BASE_DIR/dev-$i
	log_must truncate -s ${dev_size_mb}M $device
	disks[${#disks[*]}+1]=$device
done

nparity=$((RANDOM%(3) + 1))
raid=raidz$nparity

log_must zpool create -f -o cachefile=$cachefile $TESTPOOL $raid \
    ${disks[1..$(($nparity+1))]}
log_must zfs create -o recordsize=8k $TESTPOOL/fs
log_must fill_fs /$TESTPOOL/fs 1 128 102400 1 R

# Pause the reflow about halfway through.
reflow_size=$(get_pool_prop allocated $TESTPOOL)
log_must set_tunable64 RAIDZ_EXPAND_MAX_REFLOW_BYTES $((reflow_size / 2))
log_must zpool attach $TESTPOOL ${raid}-0 ${disks[$(($nparity+2))]}
wait_raidz_expand_paused $TESTPOOL

log_must cp $cachefile $stale
log_must eval "zdb -C -U $stale | grep -q raidz_expanding"

log_must set_tunable64 RAIDZ_EXPAND_MAX_REFLOW_BYTES 0
log_must zpool wait -t raidz_expand $TESTPOOL
log_must zfs create -o recordsize=128k $TESTPOOL/fs2
log_must fill_fs /$TESTPOOL/fs2 1 32 102400 1 R
log_must eval "find /$TESTPOOL -type f -exec cksum {} + | sort > $sums"
log_must zpool export $TESTPOOL

log_must eval "zdb -l ${disks[1]} | grep -q raidz_expand_txgs"
log_mustnot eval "zdb -l ${disks[1]} | grep -q raidz_expanding"

log_must eval "zpool import -c $stale -o cachefile=$cachefile $TESTPOOL \
    > $out 2>&1"
cat $out
log_mustnot grep -q "cachefile import failed" $out
log_must eval "kstat dbgmsg | grep -qF '$msg'"

log_must eval "find /$TESTPOOL -type f -exec cksum {} + | sort | \
    cmp -s - $sums"
log_must eval "zdb -C -U $cachefile | grep -q raidz_expand_txgs"
log_mustnot eval "zdb -C -U $cachefile | grep -q raidz_expanding"
log_must zpool scrub -w $TESTPOOL
log_must check_pool_status $TESTPOOL "errors" "No known data errors"
log_must check_pool_status $TESTPOOL "scan" "with 0 errors"

log_pass "raidz pool imports from a cachefile older than its expansion."
