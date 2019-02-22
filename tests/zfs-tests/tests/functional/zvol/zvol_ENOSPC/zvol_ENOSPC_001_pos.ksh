#!/bin/ksh -p
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
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
