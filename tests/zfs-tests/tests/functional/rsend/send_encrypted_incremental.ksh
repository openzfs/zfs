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
