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
# Copyright (c) 2020 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/rsend/rsend.kshlib

#
# Description:
# Verify that send -L can be changed to on in an incremental.
# Verify that send -L can not be turned off in an incremental.
#

function cleanup
{
	log_must_busy zfs destroy -r $TESTPOOL/fs
	log_must_busy zfs destroy -r $TESTPOOL/recv
}

verify_runnable "both"

log_assert "Verify toggling send -L works as expected"
log_onexit cleanup

log_must zfs create -o compression=on -o recordsize=1m $TESTPOOL/fs

log_must dd if=/dev/urandom of=/$TESTPOOL/fs/file bs=1024 count=1500

log_must zfs snapshot $TESTPOOL/fs@snap

log_must dd if=/dev/urandom of=/$TESTPOOL/fs/file bs=1024 count=1500 conv=notrunc seek=2048

log_must zfs snapshot $TESTPOOL/fs@snap2

log_must zfs create $TESTPOOL/recv

log_must zfs send -c $TESTPOOL/fs@snap | zfs recv $TESTPOOL/recv/noL-noL
log_must zfs send -c -i @snap $TESTPOOL/fs@snap2| zfs recv $TESTPOOL/recv/noL-noL
log_must diff /$TESTPOOL/fs/file /$TESTPOOL/recv/noL-noL/file

log_must zfs send -c -L $TESTPOOL/fs@snap | zfs recv $TESTPOOL/recv/L-L
log_must zfs send -c -L -i @snap $TESTPOOL/fs@snap2 | zfs recv $TESTPOOL/recv/L-L
log_must diff /$TESTPOOL/fs/file /$TESTPOOL/recv/L-L/file

log_must zfs send -c $TESTPOOL/fs@snap | zfs recv $TESTPOOL/recv/noL-L
log_must zfs send -c -L -i @snap $TESTPOOL/fs@snap2 | zfs recv $TESTPOOL/recv/noL-L
log_must diff /$TESTPOOL/fs/file /$TESTPOOL/recv/noL-L/file

log_must zfs send -c -L $TESTPOOL/fs@snap | zfs recv $TESTPOOL/recv/L-noL
log_mustnot zfs send -c -i @snap $TESTPOOL/fs@snap2 | zfs recv $TESTPOOL/recv/L-noL
log_must diff /$TESTPOOL/fs/.zfs/snapshot/snap/file /$TESTPOOL/recv/L-noL/file

log_pass "Verify toggling send -L works as expected"
