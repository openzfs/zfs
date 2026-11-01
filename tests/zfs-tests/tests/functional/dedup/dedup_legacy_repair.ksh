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
#	A damaged copy of a block in a legacy (traditional) dedup table is
#	repaired from another phys slot of the same entry at the next txg
#	sync (ddt_repair_table()), without crashing.
#
# STRATEGY:
#	1. Create a pool with fast_dedup disabled, so each DDT entry has one
#	   phys slot per copies= value.
#	2. Write the same block with copies=1 and with copies=2, so the entry
#	   has two independent sets of DVAs.
#	3. Export, overwrite the copies=1 DVA on the vdev, import.
#	4. Read the copies=1 file: it must be served from the copies=2 slot,
#	   which queues a DDT repair.
#	5. Sync the pool, so the repair is issued and completes.
#	6. Export/import and re-read: the copies=1 copy must now be intact.
#

verify_runnable "global"

VDEV=$TEST_BASE_DIR/dedup_repair.vdev
DATA=$TEST_BASE_DIR/dedup_repair.data

function cleanup
{
	destroy_pool $TESTPOOL
	log_must rm -f $VDEV $DATA
}

log_onexit cleanup

log_assert "legacy DDT self-heal repair from another phys slot works"

log_must truncate -s 256M $VDEV
log_must zpool create -f -o feature@fast_dedup=disabled -O dedup=sha256 \
    -O compression=off -O recordsize=128k $TESTPOOL $VDEV

log_must dd if=/dev/urandom of=$DATA bs=128k count=1
# dd rather than cp, so the second write is not a block clone
log_must dd if=$DATA of=/$TESTPOOL/single bs=128k
log_must zfs set copies=2 $TESTPOOL
log_must dd if=$DATA of=/$TESTPOOL/double bs=128k
log_must zpool sync $TESTPOOL

# one entry, holding both the single and the double copies
log_must eval "zdb -D $TESTPOOL | grep -q 'DDT-sha256-zap-duplicate:.*entries=1'"

obj=$(get_objnum /$TESTPOOL/single)
dva=$(zdb -ddddd $TESTPOOL/ $obj | awk '/ L0 /{print $3; exit}')
log_note "copies=1 L0 DVA: $dva"
[[ -n "$dva" ]] || log_fail "could not find L0 DVA of /$TESTPOOL/single"
off=$(( 16#$(echo $dva | cut -d: -f2) + 16#400000 ))

log_must zpool export $TESTPOOL
log_must dd if=/dev/urandom of=$VDEV bs=512 seek=$((off / 512)) count=256 \
    conv=notrunc
log_must zpool import -d $TEST_BASE_DIR $TESTPOOL

# served from the copies=2 slot; queues a DDT repair
log_must cmp /$TESTPOOL/single $DATA
log_must zpool status -v $TESTPOOL

# repair is issued from ddt_sync() -> ddt_repair_table()
log_must zpool sync $TESTPOOL
log_must zpool sync $TESTPOOL

# the copies=1 copy must have been rewritten
log_must zpool clear $TESTPOOL
log_must zpool export $TESTPOOL
log_must zpool import -d $TEST_BASE_DIR $TESTPOOL
log_must cmp /$TESTPOOL/single $DATA
log_must zpool status -v $TESTPOOL
errs=$(zpool status -p $TESTPOOL | awk -v v=$VDEV '$1 == v {print $5}')
[[ "$errs" == "0" ]] || log_fail "copies=1 copy not repaired ($errs cksum)"

log_pass "legacy DDT self-heal repair from another phys slot works"
