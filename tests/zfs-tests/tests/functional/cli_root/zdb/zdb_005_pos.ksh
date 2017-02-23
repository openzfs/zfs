#!/bin/ksh

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
# Copyright (c) 2017 by Lawrence Livermore National Security, LLC.
#

. $STF_SUITE/include/libtest.shlib

#
# Description:
# zdb -l exit codes are correct
#
# Strategy:
# 1. Create a pool
# 2. Overwrite label 0 on vdev[1] with dd
# 3. Create an empty file
# 3. Run zdb -l on vdev[0] and verify exit value 0
# 4. Run zdb -l on vdev[1] and verify exit value 1
# 5. Run zdb -l on the empty file and verify exit value 2
#

log_assert "Verify zdb -l exit codes are correct"
log_onexit cleanup

function cleanup
{
	datasetexists $TESTPOOL && destroy_pool $TESTPOOL
}

verify_runnable "global"
verify_disk_count "$DISKS" 2

set -A DISK $DISKS

default_mirror_setup_noexit $DISKS
log_must $DD if=/dev/zero of=$DEV_RDSKDIR/${DISK[1]} bs=1K count=256 conv=notrunc
log_must $TRUNCATE -s 0 $TEMPFILE

$ZDB -l $DEV_RDSKDIR/${DISK[0]}
[[ $? -ne 0 ]] &&
	log_fail "zdb -l exit codes are incorrect."

$ZDB -l $DEV_RDSKDIR/${DISK[1]}
[[ $? -ne 1 ]] &&
	log_fail "zdb -l exit codes are incorrect."

$ZDB -l $TEMPFILE
[[ $? -ne 2 ]] &&
	log_fail "zdb -l exit codes are incorrect."

cleanup

log_pass "zdb -l exit codes are correct."
