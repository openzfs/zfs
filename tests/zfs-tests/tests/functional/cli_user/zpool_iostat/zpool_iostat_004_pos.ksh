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
# Copyright (c) 2013 by Delphix. All rights reserved.
#

# Copyright (C) 2016 Lawrence Livermore National Security, LLC.

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Executing 'zpool iostat' command with various combinations of extended
# stats (-lqwr), parsable/script options (-pH), and misc lists of pools
# and vdevs.
#
# STRATEGY:
# 1. Create an array of mixed 'zpool iostat' options.
# 2. Execute each element of the array.
# 3. Verify an error code is returned.
#

verify_runnable "both"

typeset testpool
if is_global_zone ; then
        testpool=$TESTPOOL
else
        testpool=${TESTPOOL%%/*}
fi

set -A args "" "-v" "-q" "-l" "-lq $TESTPOOL" "-ql ${DISKS[0]} ${DISKS[1]}" \
	"-w $TESTPOOL ${DISKS[0]} ${DISKS[1]}" \
	"-wp $TESTPOOL" \
	"-qlH $TESTPOOL ${DISKS[0]}" \
	"-vpH ${DISKS[0]}" \
	"-wpH ${DISKS[0]}" \
	"-r ${DISKS[0]}" \
	"-rpH ${DISKS[0]}"

log_assert "Executing 'zpool iostat' with extended stat options succeeds"
log_note "testpool: $TESTPOOL, disks $DISKS"

typeset -i i=1
while [[ $i -lt ${#args[*]} ]]; do
	log_note "doing zpool iostat ${args[i]}"
	log_must zpool iostat ${args[i]}
	((i = i + 1))
done

log_pass "Executing 'zpool iostat' with extended stat options succeeds"
