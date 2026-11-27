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
# ran must not report checksum errors on healthy devices or permanent errors
# in the pool's metadata.
#
# When dsl_scan_sync() cancels the running resilver and sets up the new one,
# it does so in sync pass 1 of the current txg, after the MOS was written.
# The new resilver's scn_max_txg is the end of the new device's DTL, a few
# txgs past the current one, so it queued I/O for MOS blocks born in this
# txg.  A later pass of the same sync then rewrote some of them in place
# (zfs_sync_pass_rewrite), and the queued reads found newer contents than
# the checksums they were queued with: every copy failed on every child.
#
# STRATEGY:
# 1. Create a two-way mirror and write 1024 small files.
# 2. Restart resilvers at once (resilver_disable_defer=1) and slow the
#    reads down so the first resilver is still running.
# 3. Attach a third device, then a fourth while the first resilver runs.
# 4. Check that the resilver found no errors and
#    that no device has checksum errors.
# 5. Repeat a few times.
#

function cleanup
{
	zinject -c all >/dev/null 2>&1
	destroy_pool $TESTPOOL1
	rm -f ${VDEV_FILES[@]} $SPARE_VDEV_FILE $SPARE_VDEV_FILE2
	log_must set_tunable32 RESILVER_DISABLE_DEFER $ORIG_DISABLE_DEFER
}

log_assert "A restarted resilver does not report errors on healthy devices"

ORIG_DISABLE_DEFER=$(get_tunable RESILVER_DISABLE_DEFER)
log_onexit cleanup

# Restart the resilver as soon as another device needs one.
log_must set_tunable32 RESILVER_DISABLE_DEFER 1

for round in 1 2 3; do
	log_must truncate -s 256m ${VDEV_FILES[0]} \
	    ${VDEV_FILES[1]} $SPARE_VDEV_FILE $SPARE_VDEV_FILE2
	log_must zpool create -f $TESTPOOL1 mirror ${VDEV_FILES[0]} \
	    ${VDEV_FILES[1]}
	# Many small blocks, so the slowed resilver takes a while.
	for i in {1..1024}; do
		dd if=/dev/urandom of=/$TESTPOOL1/file$i bs=100k count=1 \
		    2>/dev/null || log_fail "writing file$i failed"
	done
	sync_pool $TESTPOOL1

	log_must zinject -D 20:1 -d ${VDEV_FILES[0]} $TESTPOOL1
	log_must zinject -D 20:1 -d ${VDEV_FILES[1]} $TESTPOOL1

	log_must zpool attach $TESTPOOL1 ${VDEV_FILES[0]} $SPARE_VDEV_FILE
	sleep 3
	is_pool_resilvering $TESTPOOL1 || \
	    log_unsupported "the first resilver finished too soon"
	log_must zpool attach $TESTPOOL1 ${VDEV_FILES[0]} $SPARE_VDEV_FILE2
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
	log_must check_pool_status $TESTPOOL1 "scan" "with 0 errors"
	log_must check_pool_status $TESTPOOL1 "errors" "No known data errors"
	for vdev in ${VDEV_FILES[0]} ${VDEV_FILES[1]} $SPARE_VDEV_FILE \
	    $SPARE_VDEV_FILE2; do
		cksum=$(zpool status -p $TESTPOOL1 | \
		    awk -v d=$vdev '$1 == d {print $5}')
		[[ "$cksum" == "0" ]] || \
		    log_fail "$vdev has $cksum checksum errors"
	done

	destroy_pool $TESTPOOL1
done

log_pass "A restarted resilver does not report errors on healthy devices"
