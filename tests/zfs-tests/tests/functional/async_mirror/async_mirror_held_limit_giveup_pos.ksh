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

#
# Copyright (c) 2026, John White.
#

. $STF_SUITE/tests/functional/async_mirror/async_mirror.kshlib

#
# DESCRIPTION:
#	When the held space that waits for a stalled lagging child reaches
#	its hard limit, the async mirror gives up on the child and freezes
#	it: the held space comes back, writes do not wait, and the child
#	stays a complete pool at its own txg.  When the child answers
#	quickly again, the child stays frozen until the administrator starts
#	a resilver. The administrator can then enable asynchronous writes.
#
# STRATEGY:
#	1. Create a pool with a two-way mirror and turn the async mirror on.
#	   Write two small files and a file of 40 MiB, about a third of
#	   the pool, and wait until the lagging child has caught up.
#	2. Set the hard limit to 10% of the pool.  Stop the copy, delay each
#	   I/O of the lagging child, remove the large file and sync the
#	   pool until the child is frozen.
#	3. Verify the kstat, the event with its reason, and the text of
#	   'zpool status'.  Verify that no failure was reported, that a write
#	   with a time limit ends, that the held space comes back, and that
#	   a file as large as the removed one can be written.
#	4. Remove the delay and export the pool. Verify that the lagging
#	   child is still at its frozen txg.  Import it alone, read-only,
#	   and verify that it holds the small files and the large file,
#	   and no file written after the freeze.
#	5. Import the whole pool.  Verify that the child is still frozen,
#	   with the same reason.
#	6. Set write_mode=sync on the child, and wait for its normal
#	   resilver. Then set write_mode=behind again and verify the async
#	   mirror copies normally.
#	7. Verify the files and a scrub.  Export the pool, import the
#	   lagging child alone, and verify the files and a scrub again.
#

verify_runnable "global"

typeset event_file=$TEST_BASE_DIR/async_mirror_held_limit_events.$$

function cleanup
{
	zinject -c all > /dev/null 2>&1
	[[ -e $ASYNC_MIRROR_HIDDEN ]] && poolexists $TESTPOOL && \
	    zpool export $TESTPOOL
	[[ -e $ASYNC_MIRROR_HIDDEN ]] && async_mirror_unhide_leader
	cleanup_async_mirror
	rm -f $event_file
}

log_onexit cleanup
log_assert "The async mirror freezes a stalled lagging child at the hard" \
    "limit of the held space until an administrator starts a resilver."

typeset -i mib=$(( 1024 * 1024 ))
typeset -i i held frozen avail_before avail_after
typeset status digests

async_mirror_create_pool
log_must zfs set compression=off $TESTPOOL
async_mirror_enable

async_mirror_write_files keep 2
async_mirror_write /$TESTPOOL/big 320
sync_pool $TESTPOOL
log_must wait_async_mirror_idle
digests="$(async_mirror_digests keep 2)"
avail_before=$(zfs get -Hp -o value available $TESTPOOL)

log_must zpool events -c
async_mirror_freeze_at_held_limit /$TESTPOOL/big
frozen=$(get_async_mirror_stat frozen_txg)
log_must test $frozen -eq $(get_async_mirror_stat applied_txg)

log_must eval "zpool events -v > $event_file"
log_must grep -q 'sysevent\.fs\.zfs\.async_mirror_frozen' $event_file
log_must grep -q 'async_mirror_reason = "held space limit"' $event_file
log_mustnot grep -q 'sysevent\.fs\.zfs\.async_mirror_failed' $event_file

status=$(zpool status $TESTPOOL)
log_note "$status"
echo "$status" | grep -q 'gave up on the lagging' ||
	log_fail "The status does not say that the pool gave up on the child"
echo "$status" | grep -q \
	'not protected: gave up (held space limit), the child needs a resilver' ||
	log_fail "The async mirror line does not show the held space limit"

# A write does not wait for the lagging child.
log_must timeout 60 dd if=/dev/urandom of=/$TESTPOOL/during bs=128k \
    count=8 conv=fsync

#
# The records are released with the freeze entry, in the txgs after the
# freeze.  The freed space is available two txgs later.
#
for (( i = 0; i < ASYNC_MIRROR_TIMEOUT; i++ )); do
	avail_after=$(zfs get -Hp -o value available $TESTPOOL)
	(( $(get_async_mirror_stat held_bytes) < mib &&
	    avail_after >= avail_before + 30 * mib )) && break
	sync_pool $TESTPOOL
	sleep 1
done
held=$(get_async_mirror_stat held_bytes)
log_note "$held bytes held after the freeze;" \
    "available $avail_before before the frees, $avail_after now"
log_must test $held -lt $mib
log_must test $avail_after -ge $(( avail_before + 30 * mib ))
log_must timeout 60 file_write -o create -f /$TESTPOOL/again -b 131072 \
    -c 320 -d R
digests="$digests
$(xxh128digest /$TESTPOOL/again)"

log_must test "$(get_async_mirror_prop state)" == frozen

# The lagging child alone is the pool of its frozen txg.
log_must zinject -c all
log_must zpool export $TESTPOOL
log_must test $(get_ring_max_txg $ASYNC_MIRROR_LAGGING) -eq $frozen
log_must mv $ASYNC_MIRROR_LEADER $ASYNC_MIRROR_HIDDEN
log_must zpool import -o readonly=on -d $ASYNC_MIRROR_DIR $TESTPOOL
log_must test "$(async_mirror_digests keep 2)" == \
    "$(echo "$digests" | head -n 2)"
log_must test -e /$TESTPOOL/big
log_mustnot test -e /$TESTPOOL/during
log_mustnot test -e /$TESTPOOL/again
log_must check_pool_status $TESTPOOL errors "No known data errors"
log_must zpool export $TESTPOOL
async_mirror_unhide_leader

# The freeze and its reason are kept across the import.
async_mirror_import
log_must test "$(get_async_mirror_stat frozen)" -eq 1
log_must test "$(get_async_mirror_stat held_limit)" -eq 1
log_must test "$(get_async_mirror_stat frozen_txg)" -eq $frozen
log_must test "$(get_async_mirror_prop state)" == frozen

# The administrator starts a normal resilver.
async_mirror_suspend_copy 0
log_must zpool set write_mode=sync $TESTPOOL $ASYNC_MIRROR_LAGGING
log_must wait_async_mirror_resilvered
log_must test "$(get_async_mirror_prop)" == sync
log_must zpool set write_mode=behind $TESTPOOL $ASYNC_MIRROR_LAGGING
log_must wait_async_mirror_state "behind|current"
log_must wait_async_mirror_idle
log_must test "$(get_async_mirror_stat frozen)" -eq 0
log_must test "$(get_async_mirror_stat failed)" -eq 0
log_must test "$(get_async_mirror_stat held_limit)" -eq 0
log_must test "$(get_async_mirror_stat applied_txg)" -gt $frozen
status=$(zpool status $TESTPOOL)
log_note "$status"
echo "$status" | grep -q 'not protected' &&
	log_fail "The status still shows the give-up"

log_must test "$digests" == "$(async_mirror_digests keep 2)
$(xxh128digest /$TESTPOOL/again)"
async_mirror_check_clean $ASYNC_MIRROR_LAGGING
log_must zpool export $TESTPOOL

async_mirror_import_lagging
log_must test "$digests" == "$(async_mirror_digests keep 2)
$(xxh128digest /$TESTPOOL/again)"
log_mustnot test -e /$TESTPOOL/big
async_mirror_check_clean $ASYNC_MIRROR_LAGGING
log_must zpool export $TESTPOOL
async_mirror_unhide_leader

log_pass "The async mirror freezes a stalled lagging child at the hard" \
    "limit of the held space until an administrator starts a resilver."
