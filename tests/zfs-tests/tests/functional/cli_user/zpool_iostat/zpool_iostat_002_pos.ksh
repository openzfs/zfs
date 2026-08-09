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
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Verify that 'zpool iostat [interval [count]]' can be executed as non-root.
#
# STRATEGY:
# 1. Set the interval to 1 and count to 4.
# 2. Verify that the output has 4 records.
# 3. Set interval to 0.5 and count to 1 to test floating point intervals.

verify_runnable "both"

typeset tmpfile=$TEST_BASE_DIR/zfsiostat.out.$$
typeset -i stat_count=0

function cleanup
{
	if [[ -f $tmpfile ]]; then
		rm -f $tmpfile
	fi
}

log_onexit cleanup
log_assert "zpool iostat [pool_name ...] [interval] [count]"

if ! is_global_zone ; then
	TESTPOOL=${TESTPOOL%%/*}
fi

log_must eval "zpool iostat $TESTPOOL 1 4 > $tmpfile 2>&1"
stat_count=$(grep -c $TESTPOOL $tmpfile)

if [[ $stat_count -ne 4 ]]; then
	cat $tmpfile
	log_fail "zpool iostat [pool_name] [interval] [count] failed"
fi

# Test a floating point interval value
log_must zpool iostat -v 0.5 1

log_pass "zpool iostat [pool_name ...] [interval] [count] passed"
