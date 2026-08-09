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
# Copyright 2017, loli10K. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/no_space/enospc.cfg
. $STF_SUITE/tests/functional/cli_root/zpool_create/zpool_create.shlib

#
# DESCRIPTION:
#	ENOSPC is returned on pools with large physical block size and small
#	recordsize.
#
# STRATEGY:
#	1. Create a pool with property ashift=13 (8K block size)
#	2. Set property recordsize=512 and copies=3 on the root dataset
#	3. Write a file until the file system is full
#	4. Verify the return code is ENOSPC
#

verify_runnable "both"

function cleanup
{
	poolexists $TESTPOOL1 && destroy_pool $TESTPOOL1
	rm -f $testfile0
}

log_onexit cleanup

log_assert "ENOSPC is returned on pools with large physical block size"

typeset testfile0=${TESTDIR}/testfile0

log_must zpool create -o ashift=13 $TESTPOOL1 $DISK_LARGE
log_must zfs set mountpoint=$TESTDIR $TESTPOOL1
log_must zfs set compression=off $TESTPOOL1
log_must zfs set recordsize=512 $TESTPOOL1
log_must zfs set copies=3 $TESTPOOL1

log_note "Writing file: $testfile0 until ENOSPC."
file_write -o create -f $testfile0 -b $BLOCKSZ \
    -c $NUM_WRITES -d $DATA
ret=$?

(( $ret != $ENOSPC )) && \
    log_fail "$testfile0 returned: $ret rather than ENOSPC."

log_pass "ENOSPC returned as expected."
