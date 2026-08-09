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
# Copyright (c) 2022, George Amanakis. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/rsend/rsend.kshlib

#
# Description:
# Receiving a snapshot with large blocks and raw sending it succeeds.
#
# Strategy:
# 1) Create a set of files each containing some file data in an
#	encrypted filesystem with recordsize=1m.
# 2) Snapshot and send with large_blocks enabled to a new filesystem.
# 3) Raw send to a file. If the large_blocks feature is not activated
#	in the filesystem created in (2) the raw send will fail.
#

verify_runnable "both"

log_assert "Receiving and raw sending a snapshot with large blocks succeeds"

backup=$TEST_BASE_DIR/backup
raw_backup=$TEST_BASE_DIR/raw_backup

function cleanup
{
	log_must rm -f $backup $raw_backup $ibackup $unc_backup
	destroy_pool pool_lb
	log_must rm -f $TESTDIR/vdev_a
}

log_onexit cleanup

typeset passphrase="password"
typeset file="/pool_lb/fs/$TESTFILE0"

# Create pool
truncate -s $MINVDEVSIZE $TESTDIR/vdev_a
log_must zpool create -f -o feature@large_blocks=enabled pool_lb $TESTDIR/vdev_a

log_must eval "echo $passphrase > /pool_lb/pwd"

log_must zfs create -o recordsize=1m pool_lb/fs
log_must dd if=/dev/urandom of=$file bs=1024 count=1024
log_must zfs snapshot pool_lb/fs@snap1

log_must eval "zfs send -L pool_lb/fs@snap1 > $backup"
log_must eval "zfs recv -o encryption=aes-256-ccm -o keyformat=passphrase \
    -o keylocation=file:///pool_lb/pwd -o primarycache=none \
    -o recordsize=1m pool_lb/testfs5 < $backup"

log_must eval "zfs send --raw pool_lb/testfs5@snap1 > $raw_backup"

log_pass "Receiving and raw sending a snapshot with large blocks succeeds"
