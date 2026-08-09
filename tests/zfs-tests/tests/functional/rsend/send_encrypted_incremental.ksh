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
. $STF_SUITE/tests/functional/rsend/rsend.kshlib

#
# Description:
# Incrementally receiving a snapshot to an encrypted filesystem succeeds.
#
# Strategy:
# 1) Create a pool and an encrypted fs
# 2) Create some files and snapshots
# 3) Send the first snapshot to a second encrypted as well as an
#	unencrypted fs.
# 4) Incrementally send the second snapshot to the unencrypted fs.
# 5) Rollback the second encrypted fs to the first snapshot.
# 6) Incrementally send the second snapshot from the unencrypted to the
#	second encrypted fs.
# 7) Incrementally send the third snapshot from the first encrypted to the
#	unencrypted fs.
# 8) Incrementally send the third snapshot from the unencrypted to the second
#	encrypted fs.
#

verify_runnable "both"

log_assert "Incrementally receiving a snapshot to an encrypted filesystem succeeds"

function cleanup
{
	destroy_pool pool_lb
	log_must rm -f $TESTDIR/vdev_a
}

log_onexit cleanup

typeset passphrase="password"
typeset passphrase2="password2"

typeset file="/pool_lb/encryptme/$TESTFILE0"
typeset file1="/pool_lb/encryptme/$TESTFILE1"
typeset file2="/pool_lb/encryptme/$TESTFILE2"

# Create pool
truncate -s $MINVDEVSIZE $TESTDIR/vdev_a
log_must zpool create -f pool_lb $TESTDIR/vdev_a
log_must eval "echo $passphrase > /pool_lb/pwd"
log_must eval "echo $passphrase2 > /pool_lb/pwd2"

log_must zfs create -o encryption=on -o keyformat=passphrase \
	-o keylocation=file:///pool_lb/pwd pool_lb/encryptme
log_must dd if=/dev/urandom of=$file bs=1024 count=1024
log_must zfs snapshot pool_lb/encryptme@snap1
log_must dd if=/dev/urandom of=$file1 bs=1024 count=1024
log_must zfs snapshot pool_lb/encryptme@snap2
log_must dd if=/dev/urandom of=$file2 bs=1024 count=1024
log_must zfs snapshot pool_lb/encryptme@snap3
log_must eval "zfs send -Lc pool_lb/encryptme@snap1 | zfs recv \
	-o encryption=on -o keyformat=passphrase -o keylocation=file:///pool_lb/pwd2 \
	pool_lb/encrypttwo"
log_must eval "zfs send -Lc pool_lb/encryptme@snap1 | zfs recv \
	pool_lb/unencryptme"
log_must eval "zfs send -Lc -i pool_lb/encryptme@{snap1,snap2} | zfs recv \
	pool_lb/unencryptme"
log_must zfs rollback pool_lb/encrypttwo@snap1
log_must eval "zfs send -Lc -i pool_lb/unencryptme@{snap1,snap2} | zfs recv \
	pool_lb/encrypttwo"
log_must eval "zfs send -Lc -i pool_lb/encryptme@{snap2,snap3} | zfs recv \
	pool_lb/unencryptme"
log_must eval "zfs send -Lc -i pool_lb/unencryptme@{snap2,snap3} | zfs recv \
	-F pool_lb/encrypttwo"

log_pass "Incrementally receiving a snapshot to an encrypted filesystem succeeds"
