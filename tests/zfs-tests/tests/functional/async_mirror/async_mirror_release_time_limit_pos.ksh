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
#	One release sync task of the async mirror has a time limit.  A
#	release of many held ranges that reaches the limit stops, keeps the
#	rest of the record, and continues in a later txg, so that the sync
#	of one txg does not take the time of the whole release.
#
# STRATEGY:
#	1. Set the txg timeout to 5 minutes, so that the pool syncs no txg
#	   on its own, and the time limit of a release to 1 ms.
#	2. Create a pool with a two-way mirror and turn the async mirror on.
#	3. Stop the release.  Write a file of 512-byte blocks, and write
#	   every second block again: 40 MiB in about 80000 held ranges,
#	   and the indirect blocks of the file.  The held space must be far
#	   below a quarter of the held space limit of the pool, so that no
#	   release runs in a txg of its own.  Wait until the lagging child
#	   has caught up.
#	4. Restart the release.  Sync one txg at a time, again and again,
#	   and count the txgs after which at least 16 KiB of held space
#	   came back, until three such steps are counted, or less than 1
#	   MiB is held.  One txg has one release.  "zpool sync" syncs three
#	   txgs, so it is not used here: three releases follow each other
#	   in them, and on a fast machine they free every range.
#	5. Verify that the space came back in at least two such steps.
#	   With no time limit, one release frees every range of a record.
#	6. Set the time limit back to its first value, and sync the pool
#	   again and again until less than 1 MiB is held.  With the limit of
#	   1 ms, each release takes only a few of the small records that the
#	   syncs add, so the end of the wait uses the first limit.
#	7. Verify that the copy did not fail, and the pool.
#

verify_runnable "global"

log_onexit cleanup_async_mirror

ASYNC_MIRROR_VDEV_SIZE=2g
typeset -i mib=$(( 1024 * 1024 ))
typeset -i blocks=163840
typeset src=$TEST_BASE_DIR/async_mirror_time_limit.$$
typeset -i held last quarter steps=0 i size normal_ms

normal_ms=$(get_tunable ASYNC_MIRROR_RELEASE_MS)
log_must async_mirror_set_tunable32 TXG_TIMEOUT 300
log_must async_mirror_set_tunable32 ASYNC_MIRROR_RELEASE_MS 1
async_mirror_create_pool
log_must zfs set recordsize=512 $TESTPOOL
log_must zfs set compression=off $TESTPOOL
async_mirror_enable

# A release runs in a txg of its own above this much held space.
size=$(get_pool_prop size $TESTPOOL)
quarter=$(( size / 100 * 25 / 4 ))

async_mirror_suspend_release 1
log_must dd if=/dev/urandom of=$src bs=512 count=$blocks
log_must dd if=$src of=/$TESTPOOL/ranges bs=512
sync_pool $TESTPOOL
log_must wait_async_mirror_idle
log_must stride_dd -i $src -o /$TESTPOOL/ranges -b 512 \
    -c $(( blocks / 2 )) -s 2
log_must rm -f $src
sync_pool $TESTPOOL
log_must wait_async_mirror_idle

held=$(get_async_mirror_stat held_bytes)
log_note "held space before the release: $held bytes," \
    "a quarter of the limit is $quarter bytes"
log_must test $held -ge $(( 32 * mib ))
log_must test $held -lt $(( quarter - 6 * mib ))

async_mirror_suspend_release 0
last=$held
for (( i = 0; i < 100 && held >= mib && steps < 3; i++ )); do
	#
	# The copy of each txg wakes the release, which joins the next txg.
	# The change of a property syncs its own txg and no other one.
	#
	async_mirror_write /$TESTPOOL/kick 1
	log_must zfs set org.openzfs.test:kick=$i $TESTPOOL
	log_must wait_async_mirror_idle
	sleep 1
	held=$(get_async_mirror_stat held_bytes)
	(( held + 16 * 1024 <= last )) && (( steps++ ))
	last=$held
done
log_note "the held space came back in $steps steps, $i txgs"
log_must test $steps -ge 2

# The first limit again: the small records of the kick writes now go at once.
log_must async_mirror_set_tunable32 ASYNC_MIRROR_RELEASE_MS $normal_ms
for (( i = 0; i < 100 && held >= mib; i++ )); do
	async_mirror_write /$TESTPOOL/kick 1
	sync_pool $TESTPOOL
	log_must wait_async_mirror_idle
	sleep 1
	held=$(get_async_mirror_stat held_bytes)
done
log_note "$held bytes held after $i more syncs"
log_must test $held -lt $mib

log_must test "$(get_async_mirror_stat failed)" -eq 0
log_must verify_pool $TESTPOOL

log_pass "A release that reaches its time limit continues in a later txg."
