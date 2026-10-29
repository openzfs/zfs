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
# Copyright (c) 2026, Patrick Domack. All rights reserved.
#

. $STF_SUITE/tests/functional/clonedup/clonedup.kshlib

#
# DESCRIPTION:
#	zfs send of an unmounted filesystem owns it for the send.  The
#	apply owns an unmounted filesystem it writes to, and the send
#	asks it to let go instead of failing with EBUSY.
#
# STRATEGY:
#	1. Write a file, snapshot it and remove it, and copy it twice
#	   into another filesystem, then unmount that filesystem.
#	2. Delay the pool's writes, apply slowly, and send the
#	   filesystem three times while the apply owns it.  The apply
#	   lets go when a txg closes.  A send that starts before that
#	   txg's clones are on disk reads blocks still being written.
#	3. Check that every send succeeded, that at least one found
#	   the apply holding a dataset, and that the run still cloned.
#

verify_runnable "global"

typeset -ri nblk=64

function cleanup
{
	zinject -c all >/dev/null 2>&1
	log_must restore_tunable CLONEDUP_APPLY_TXG_DELAY
	log_must restore_tunable CLONEDUP_APPLY_BLOCKS_PER_TXG
	clonedup_cleanup
}

log_assert "zfs send of a filesystem succeeds while the apply owns it"
log_onexit cleanup
log_must save_tunable CLONEDUP_APPLY_TXG_DELAY
log_must save_tunable CLONEDUP_APPLY_BLOCKS_PER_TXG

clonedup_pool_create

clonedup_write /$TESTPOOL/a $nblk
clonedup_sync
log_must zfs snapshot $TESTPOOL@snap
log_must rm /$TESTPOOL/a
clonedup_sync

log_must zfs create $TESTPOOL/dd
log_must dd if=/$TESTPOOL/.zfs/snapshot/snap/a of=/$TESTPOOL/dd/b \
    bs=$CD_BS status=none
log_must dd if=/$TESTPOOL/.zfs/snapshot/snap/a of=/$TESTPOOL/dd/c \
    bs=$CD_BS status=none
clonedup_sync
log_must zfs unmount $TESTPOOL/dd

log_must set_tunable32 CLONEDUP_APPLY_TXG_DELAY 200
log_must set_tunable32 CLONEDUP_APPLY_BLOCKS_PER_TXG 4

log_must zpool clonedup $TESTPOOL
clonedup_wait_applying
clonedup_delay_writes 25
typeset -i yields=$(clonedup_kstat yields)
for i in 1 2 3; do
	sleep 1
	log_must eval "zfs send $TESTPOOL/dd > /dev/null"
done
log_must zinject -c all
clonedup_kstat_gt yields $yields

log_must set_tunable32 CLONEDUP_APPLY_TXG_DELAY 0
log_must zpool wait -t clonedup $TESTPOOL
clonedup_jstat_gt clonedup_applied 0
clonedup_jstat_is errors 0
clonedup_leakcheck

log_pass "zfs send of a filesystem succeeds while the apply owns it"
