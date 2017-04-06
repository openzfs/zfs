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
