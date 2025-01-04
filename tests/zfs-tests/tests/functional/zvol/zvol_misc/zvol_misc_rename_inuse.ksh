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
# http://www.illumos.org/license/CDDL.
#

#
# Copyright 2019, loli10K <ezomori.nozomu@gmail.com>. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/zvol/zvol_common.shlib
. $STF_SUITE/tests/functional/zvol/zvol_misc/zvol_misc_common.kshlib

#
# DESCRIPTION:
# Verify 'zfs rename' works on a ZVOL already in use as block device
#
# STRATEGY:
# 1. Create a ZVOL
# 2. Create a filesystem on the ZVOL device and mount it
# 3. Rename the ZVOL dataset
# 4. Receive a send stream with the same name as the old ZVOL dataset and verify
#    we don't trigger any issue like the one reported in #6263
#

verify_runnable "global"

function cleanup
{
	log_must umount "$MNTPFS"
	log_must rmdir "$MNTPFS"
	for ds in "$SENDFS" "$ZVOL" "$ZVOL-renamed"; do
		destroy_dataset "$ds" '-rf'
	done
	block_device_wait
}

log_assert "Verify 'zfs rename' works on a ZVOL already in use as block device"
log_onexit cleanup

ZVOL="$TESTPOOL/vol.$$"
ZDEV="$ZVOL_DEVDIR/$ZVOL"
MNTPFS="$TESTDIR/zvol_inuse_rename"
SENDFS="$TESTPOOL/sendfs.$$"

# 1. Create a ZVOL
log_must zfs create -V $VOLSIZE "$ZVOL"

# 2. Create a filesystem on the ZVOL device and mount it
block_device_wait "$ZDEV"
log_must eval "new_fs $ZDEV >/dev/null 2>&1"
log_must mkdir "$MNTPFS"
log_must mount "$ZDEV" "$MNTPFS"

# 3. Rename the ZVOL dataset
log_must zfs rename "$ZVOL" "$ZVOL-renamed"

# 4. Receive a send stream with the same name as the old ZVOL dataset and verify
#    we don't trigger any issue like the one reported in #6263
log_must zfs create "$SENDFS"
log_must zfs snap "$SENDFS@snap"
log_must eval "zfs send $SENDFS@snap | zfs recv $ZVOL"

log_pass "Renaming in use ZVOL works successfully"
