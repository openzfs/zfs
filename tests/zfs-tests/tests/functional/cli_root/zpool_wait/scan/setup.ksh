#!/bin/ksh -p
#
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
# Copyright (c) 2018 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_wait/zpool_wait.kshlib

verify_runnable "global"
verify_disk_count $DISKS 3

#
# Set up a pool for use in the tests that do scrubbing and resilvering. Each
# test leaves the pool in the same state as when it started, so it is safe to
# share the same setup.
#
log_must zpool create -f $TESTPOOL $DISK1
log_must dd if=/dev/urandom of="/$TESTPOOL/testfile" bs=1k count=256k

log_pass
