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
. $STF_SUITE/tests/functional/replacement/replacement.cfg

#
# DESCRIPTION:
# A resilver that is restarted because another device was attached while it
# ran must copy every block the new device is missing, including blocks
# that dmu_sync() wrote just before the attach.
#
# spa_vdev_attach() marks the txgs up to TXG_CONCURRENT_STATES past the
# attach txg as missing on the new device, because dmu_sync() may already
# have written blocks of those open txgs without it; they are stitched
# into their datasets only when their txg syncs.  The resilver must not be
# set up before then: a scan set up sooner does not find those blocks,
# and when it completes it still excises their txgs from the DTL.
#
# STRATEGY:
# 1. Create a two-way mirror with small files and a logbias=throughput
#    dataset, so every O_SYNC write is written by dmu_sync().
# 2. Start writers that write each block of their files once, with O_SYNC.
# 3. Restart resilvers at once (resilver_disable_defer=1), slow the reads
#    down, attach a third device, then a fourth while the first resilver
#    runs.
# 4. Stop the writers, wait for the resilver, clear the error counters
#    and scrub.  The scrub must repair nothing and no device may have
#    checksum errors: the resilver left nothing out.
# 5. Repeat a few times.
#

function cleanup
{
	touch $STOPFILE
	wait
	zinject -c all >/dev/null 2>&1
	destroy_pool $TESTPOOL1
	rm -f ${VDEV_FILES[@]} $SPARE_VDEV_FILE $SPARE_VDEV_FILE2 $STOPFILE
	log_must set_tunable32 RESILVER_DISABLE_DEFER $ORIG_DISABLE_DEFER
}

log_assert "A restarted resilver copies blocks written by dmu_sync()" \
    "before the attach"

ORIG_DISABLE_DEFER=$(get_tunable RESILVER_DISABLE_DEFER)
STOPFILE=$TEST_BASE_DIR/resilver_restart_004.stop
log_onexit cleanup

# Restart the resilver as soon as another device needs one.
log_must set_tunable32 RESILVER_DISABLE_DEFER 1

for round in 1 2 3; do
	rm -f $STOPFILE
	log_must truncate -s 1g ${VDEV_FILES[0]} \
	    ${VDEV_FILES[1]} $SPARE_VDEV_FILE $SPARE_VDEV_FILE2
	log_must zpool create -f $TESTPOOL1 mirror ${VDEV_FILES[0]} \
	    ${VDEV_FILES[1]}
	log_must zfs create -o logbias=throughput -o recordsize=128k \
	    -o compression=off $TESTPOOL1/fs
	# The writers' files first, so the resilver visits them first.
	for w in 1 2 3 4; do
		log_must touch /$TESTPOOL1/fs/sync$w
	done
	# Many small blocks, so the slowed resilver takes a while.
	for i in {1..512}; do
		dd if=/dev/urandom of=/$TESTPOOL1/fs/file$i bs=100k count=1 \
		    2>/dev/null || log_fail "writing file$i failed"
	done
	sync_pool $TESTPOOL1

	# Each writer writes every 128K block once (wrapping at 64M), so a
	# block dmu_sync() wrote just before an attach is not rewritten
	# after it.
	for w in 1 2 3 4; do
		(
			typeset -i k=0
			while [[ ! -e $STOPFILE ]]; do
				dd if=/dev/urandom of=/$TESTPOOL1/fs/sync$w \
				    bs=128k count=4 seek=$k oflag=sync \
				    conv=notrunc 2>/dev/null
				k=$(( (k + 4) % 512 ))
			done
		) &
	done
	sleep 2

	log_must zinject -D 20:1 -T read -d ${VDEV_FILES[0]} $TESTPOOL1
	log_must zinject -D 20:1 -T read -d ${VDEV_FILES[1]} $TESTPOOL1

	log_must zpool attach $TESTPOOL1 ${VDEV_FILES[0]} $SPARE_VDEV_FILE
	sleep 3
	is_pool_resilvering $TESTPOOL1 || \
	    log_unsupported "the first resilver finished too soon"
	log_must zpool attach $TESTPOOL1 ${VDEV_FILES[0]} $SPARE_VDEV_FILE2
	sleep 4
	touch $STOPFILE
	wait
	log_must zinject -c all
	log_must zpool wait -t resilver $TESTPOOL1

	log_must eval "zpool status -v $TESTPOOL1"
	# Unfixed, the second attach cancels the running resilver and sets
	# up a new one at once.  Fixed, the restart waits until the new
	# device's DTL has synced, so the first resilver may end first and
	# the fourth device is resilvered on its own.
	if zpool history -i $TESTPOOL1 | \
	    grep -qE 'scan (cancelled|aborted)'; then
		log_note "round $round: the running resilver was restarted"
	else
		log_note "round $round: the first resilver ended first"
	fi

	# Only the scrub's findings count: what the resilver found is
	# resilver_restart_003's business.
	log_must zpool clear $TESTPOOL1
	log_must zpool scrub -w $TESTPOOL1
	log_must eval "zpool status -v $TESTPOOL1"
	log_must check_pool_status $TESTPOOL1 "scan" "repaired 0B"
	log_must check_pool_status $TESTPOOL1 "errors" "No known data errors"
	for vdev in ${VDEV_FILES[0]} ${VDEV_FILES[1]} $SPARE_VDEV_FILE \
	    $SPARE_VDEV_FILE2; do
		cksum=$(zpool status -p $TESTPOOL1 | \
		    awk -v d=$vdev '$1 == d {print $5}')
		[[ "$cksum" == "0" ]] || \
		    log_fail "$vdev has $cksum checksum errors after the scrub"
	done

	destroy_pool $TESTPOOL1
done

log_pass "A restarted resilver copies blocks written by dmu_sync()" \
    "before the attach"
