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
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
#
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/no_space/enospc.cfg

#
# DESCRIPTION:
# ENOSPC is returned on an attempt to write a second file
# to a file system after a first file was written that terminated
# with ENOSPC on a cleanly initialized file system.
#
# STRATEGY:
# 1. Write a file until the file system is full.
# 2. Ensure that ENOSPC is returned.
# 3. Write a second file while the file system remains full.
# 4. Verify the return code is ENOSPC.
#

verify_runnable "both"

function cleanup
{
	default_cleanup_noexit
}

log_onexit cleanup

log_assert "ENOSPC is returned when file system is full."

default_setup_noexit $DISK_SMALL
log_must zfs set compression=off $TESTPOOL/$TESTFS

log_note "Writing file: $TESTFILE0 until ENOSPC."
file_write -o create -f $TESTDIR/$TESTFILE0 -b $BLOCKSZ \
    -c $NUM_WRITES -d $DATA
ret=$?

(( $ret != $ENOSPC )) && \
    log_fail "$TESTFILE0 returned: $ret rather than ENOSPC."

log_note "Write another file: $TESTFILE1 but expect ENOSPC."
file_write -o create -f $TESTDIR/$TESTFILE1 -b $BLOCKSZ \
    -c $NUM_WRITES -d $DATA
ret=$?

(( $ret != $ENOSPC )) && \
    log_fail "$TESTFILE1 returned: $ret rather than ENOSPC."

log_pass "ENOSPC returned as expected."
