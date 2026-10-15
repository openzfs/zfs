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
# Copyright (c) 2026 by the OpenZFS project.  All rights reserved.
#
. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	A Direct I/O write to a pool with no writable vdev fails with
#	EIO, not ENOSPC.
#
# STRATEGY:
#	1. Create a mirror, write a two-block file, and stop
#	   background txg syncs
#	2. Make both sides fail to open and reopen the pool, so the
#	   mirror cannot be allocated from
#	3. Overwrite a block with Direct I/O. It must fail with EIO,
#	   and the pool must not be suspended
#

verify_runnable "global"

log_assert "A Direct I/O write to an unwritable pool fails with EIO"

typeset errfile=$TEST_BASE_DIR/failmode_dio_unwritable.err

function cleanup
{
	zinject -c all
	zpool clear $TESTPOOL
	restore_tunable TXG_TIMEOUT
	destroy_pool $TESTPOOL
	rm -f $errfile
}

read -r DISK1 DISK2 _ <<<"$DISKS"
if [[ -z "$DISK2" ]]; then
	log_unsupported "this test needs two disks"
fi

log_must save_tunable TXG_TIMEOUT
log_onexit cleanup
log_must set_tunable32 TXG_TIMEOUT 600

log_must zpool create -f -o failmode=continue $TESTPOOL \
    mirror $DISK1 $DISK2
log_must zfs create -o recordsize=128k -o compression=off \
    $TESTPOOL/$TESTFS
typeset mnt=$(get_prop mountpoint $TESTPOOL/$TESTFS)
typeset f=$mnt/file

#
# Overwrites of existing full blocks take the Direct I/O path; a
# write into a new file's first block may not.
#
log_must dd if=/dev/urandom of=$f bs=128k count=2 conv=fsync
log_must zpool sync $TESTPOOL

log_must zinject -d $DISK1 -e nxio $TESTPOOL
log_must zinject -d $DISK2 -e nxio $TESTPOOL
log_must zpool reopen $TESTPOOL
log_must check_state $TESTPOOL "" "unavail"

typeset -i before=$(kstat_pool $TESTPOOL iostats.direct_write_count)
stride_dd -i /dev/urandom -o $f -b 131072 -c 1 -D 2>$errfile
typeset -i rc=$?
typeset -i after=$(kstat_pool $TESTPOOL iostats.direct_write_count)
log_note "the write returned $rc: $(<$errfile)"

if ((after <= before)); then
	log_fail "the write did not use Direct I/O"
fi
log_must [ "$(kstat_pool $TESTPOOL state)" != "SUSPENDED" ]
log_must [ $rc -ne 0 ]
log_mustnot grep -q "No space left on device" $errfile
log_must grep -q "Input/output error" $errfile

log_pass "A Direct I/O write to an unwritable pool fails with EIO"
