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
# Copyright (c) 2026 by MorganaFuture. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/include/kstat.shlib
. $STF_SUITE/tests/functional/zvol/zvol_common.shlib

#
# DESCRIPTION:
#	A synchronous write that exactly covers an aligned volblocksize block
#	is stored indirectly (WR_INDIRECT), so the data reaches the pool only
#	once instead of being written to the ZIL and again at TXG commit.
#	This avoids the ZVOL write amplification reported in #17677.
#
# STRATEGY:
#	1. Create a small-volblocksize ZVOL with sync=always and no SLOG.
#	2. Full aligned block sync writes must increase zil_itx_indirect_count.
#	3. Sub-block (partial) sync writes must NOT use indirect.
#	4. Indirectly logged full-block writes must survive ZIL replay with
#	   identical data (freeze, write, export, import).
#	5. With a dedicated SLOG, even full block writes must NOT use indirect
#	   (the SLOG latency optimization must be preserved).
#

verify_runnable "global"

if ! is_linux ; then
	log_unsupported "Requires dd oflag=direct for aligned full-block writes"
fi

typeset VOLBS=16384
typeset COUNT=64
typeset ZVOL="$TESTPOOL/vol"
typeset ZDEV="$ZVOL_DEVDIR/$ZVOL"
typeset slog="$(mktemp -p $TEST_BASE_DIR zvol_misc_fullblock_slog.XXXXXX)"

# A separate pool is frozen for the replay check so it cannot disturb
# $TESTPOOL, which is shared with the rest of the zvol_misc group.
typeset rpool="${TESTPOOL}_replay"
typeset rvol="$rpool/vol"
typeset rdev="$ZVOL_DEVDIR/$rvol"
typeset rvdev="$(mktemp -p $TEST_BASE_DIR zvol_misc_fullblock_vdev.XXXXXX)"

function cleanup
{
	datasetexists $ZVOL && destroy_dataset $ZVOL
	if zpool list -v $TESTPOOL | grep -q "$slog" ; then
		log_must zpool remove $TESTPOOL "$slog"
	fi
	poolexists $rpool && destroy_pool $rpool
	rm -f "$slog" "$rvdev"
	block_device_wait
}

# Number of indirect ZIL records logged for the zvol's objset so far.
function indirect_count
{
	kstat_dataset $ZVOL zil_itx_indirect_count
}

log_assert "Full aligned block sync writes to a ZVOL are stored indirectly"
log_onexit cleanup

log_must zfs create -V $VOLSIZE -b $VOLBS -o sync=always \
    -o compression=off -o volmode=dev $ZVOL
block_device_wait $ZDEV

# 1. Full aligned block sync writes: expect indirect writes.
typeset before=$(indirect_count)
log_must dd if=/dev/urandom of=$ZDEV bs=$VOLBS count=$COUNT \
    oflag=direct conv=fdatasync
typeset after=$(indirect_count)
typeset delta=$((after - before))
log_note "full-block writes: indirect_count delta=$delta (expected ~$COUNT)"
if [[ $delta -le $((COUNT / 2)) ]] ; then
	log_fail "Full block sync writes did not use indirect writes " \
	    "($delta <= $((COUNT / 2)))"
fi

# 2. Partial (sub-block) sync writes: must not use indirect.
before=$(indirect_count)
log_must dd if=/dev/urandom of=$ZDEV bs=$((VOLBS / 2)) count=$COUNT \
    oflag=direct conv=fdatasync
after=$(indirect_count)
delta=$((after - before))
log_note "partial writes: indirect_count delta=$delta (expected 0)"
if [[ $delta -ne 0 ]] ; then
	log_fail "Partial sync writes unexpectedly used indirect writes " \
	    "($delta != 0)"
fi

# 3. Indirectly logged full-block writes must replay with identical data.
#    A dedicated pool is frozen so the post-freeze writes reach disk only
#    through the ZIL; they survive an export/import only if the WR_INDIRECT
#    records and the blocks they reference are claimed and replayed
#    correctly.  Using a private pool keeps the freeze from disturbing
#    $TESTPOOL.
log_must truncate -s 512m "$rvdev"
log_must zpool create $rpool "$rvdev"
log_must zfs create -V 64m -b $VOLBS -o sync=always -o compression=off \
    -o volmode=dev $rvol
block_device_wait $rdev
# A pre-freeze sync write puts the ZIL header on disk so the post-freeze
# records are reachable at import.
log_must dd if=/dev/urandom of=$rdev bs=$VOLBS count=1 \
    oflag=direct conv=fdatasync
log_must sync_pool $rpool
log_must zpool freeze $rpool
log_must dd if=/dev/urandom of=$rdev bs=$VOLBS count=$COUNT \
    oflag=direct conv=fdatasync
typeset checksum=$(dd if=$rdev bs=$VOLBS count=$COUNT 2>/dev/null | \
    xxh128digest)
log_note "Verify transactions to replay:"
log_must zdb -iv $rvol
block_device_wait $rdev
log_must zpool export $rpool
log_must zpool import -f -d $TEST_BASE_DIR $rpool
block_device_wait $rdev
log_must test -b $rdev
typeset checksum1=$(dd if=$rdev bs=$VOLBS count=$COUNT 2>/dev/null | \
    xxh128digest)
if [[ "$checksum1" != "$checksum" ]] ; then
	log_fail "Indirect ZIL replay corrupted data ($checksum1 != $checksum)"
fi
log_must destroy_pool $rpool

# 4. With a dedicated SLOG, full block writes must stay in the log.
log_must truncate -s $MINVDEVSIZE "$slog"
log_must zpool add $TESTPOOL log "$slog"
before=$(indirect_count)
log_must dd if=/dev/urandom of=$ZDEV bs=$VOLBS count=$COUNT \
    oflag=direct conv=fdatasync
after=$(indirect_count)
delta=$((after - before))
log_note "full-block writes with SLOG: indirect_count delta=$delta (expected 0)"
if [[ $delta -ne 0 ]] ; then
	log_fail "Full block writes bypassed the SLOG via indirect writes " \
	    "($delta != 0)"
fi

log_pass "Full aligned block sync writes to a ZVOL are stored indirectly"
