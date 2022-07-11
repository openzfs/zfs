#!/bin/ksh -p
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
# 2. Sleep for 4 seconds.
# 3. Verify that the output has 4 records.
# 4. Set interval to 0.5 and count to 1 to test floating point intervals.

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

zpool iostat $TESTPOOL 1 4 > $tmpfile 2>&1 &
sleep 4
stat_count=$(grep -c $TESTPOOL $tmpfile)

if [[ $stat_count -ne 4 ]]; then
	log_fail "zpool iostat [pool_name] [interval] [count] failed"
fi

# Test a floating point interval value
log_must zpool iostat -v 0.5 1

log_pass "zpool iostat [pool_name ...] [interval] [count] passed"
