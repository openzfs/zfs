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
# Copyright (c) 2026 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	Destroying a batch of snapshots with one lzc_destroy_snaps() call
#	costs a single txg sync, not one txg per snapshot.
#
# STRATEGY:
#	1. Create a filesystem with $SNAP_COUNT snapshots.
#	2. Stop the txg timer, so the only txgs that sync during the measured
#	   window are the ones the destroy itself forces.
#	3. Destroy every snapshot with one 'zfs destroy fs@first%last', which
#	   is a single ZFS_IOC_DESTROY_SNAPS ioctl.
#	4. Verify the pool synced no more than $MAX_TXGS txgs, and that every
#	   snapshot is gone.
#

verify_runnable "both"

typeset -i SNAP_COUNT=50
typeset -i MAX_TXGS=5
typeset FS=$TESTPOOL/batchtxg
typeset -i save_timeout=0

function cleanup
{
	(( save_timeout > 0 )) && set_tunable64 TXG_TIMEOUT $save_timeout
	datasetexists $FS && destroy_dataset $FS -r
}

function last_txg
{
	kstat_pool $TESTPOOL txgs |
	    awk '$1 ~ /^[0-9]+$/ { t = $1 } END { print t }'
}

log_onexit cleanup
log_assert "A batch snapshot destroy costs one txg, not one per snapshot"

save_timeout=$(get_tunable TXG_TIMEOUT)
log_must set_tunable64 TXG_TIMEOUT 600

log_must zfs create $FS
typeset -i i=1
while (( i <= SNAP_COUNT )); do
	log_must dd if=/dev/urandom of=/$FS/f$i bs=4k count=1
	log_must zfs snapshot $FS@s$i
	(( i = i + 1 ))
done
log_must sync_pool $TESTPOOL

typeset -i before after used
before=$(last_txg)
log_must zfs destroy $FS@s1%s$SNAP_COUNT
after=$(last_txg)
used=$(( after - before ))

log_note "destroying $SNAP_COUNT snapshots in one batch synced $used txg(s)"

log_mustnot datasetexists $FS@s1
log_mustnot datasetexists $FS@s$SNAP_COUNT

if (( used > MAX_TXGS )); then
	log_fail "batch destroy of $SNAP_COUNT snapshots synced $used txgs," \
	    "expected at most $MAX_TXGS"
fi

log_pass "A batch snapshot destroy costs one txg, not one per snapshot"
