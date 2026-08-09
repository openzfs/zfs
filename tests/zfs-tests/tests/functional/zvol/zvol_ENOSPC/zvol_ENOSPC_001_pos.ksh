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
. $STF_SUITE/tests/functional/zvol/zvol.cfg

#
# DESCRIPTION:
# A zvol volume will return ENOSPC when the underlying pool runs out of
# space.
#
# STRATEGY:
# 1. Create a pool
# 2. Create a zvol volume
# 3. Create a ufs file system ontop of the zvol
# 4. Mount the ufs file system
# 5. Fill volume until ENOSPC is returned
#

verify_runnable "global"

function cleanup
{
	rm -rf $TESTDIR/*
}

log_assert "A zvol volume will return ENOSPC when the underlying pool " \
    "runs out of space."

log_onexit cleanup

typeset -i fn=0
typeset -i retval=0

BLOCKSZ=$(( 1024 * 1024 ))
NUM_WRITES=40

while (( 1 )); do
        file_write -o create -f $TESTDIR/testfile$$.$fn \
            -b $BLOCKSZ -c $NUM_WRITES
        retval=$?
        if (( $retval != 0 )); then
                break
        fi

        (( fn = fn + 1 ))
done

(( $retval != $ENOSPC )) &&
    log_fail "ENOSPC was not returned, $retval was received instead"

log_pass "ENOSPC was returned as expected"
