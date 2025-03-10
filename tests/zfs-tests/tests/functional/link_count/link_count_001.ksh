#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
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
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Verify file link count is zero on zfs
#
# STRATEGY:
# 1. Make sure this test executes on multi-processes system
# 2. Make zero size files and remove them in the background
# 3. Call the binary
# 4. Make sure the files can be removed successfully
#

verify_runnable "both"

log_assert "Verify file link count is zero on zfs"

export ITERS=10
export NUMFILES=10000

if is_freebsd; then
	log_unsupported "Not applicable on FreeBSD"
fi

# Detect and make sure this test must be executed on a multi-process system
if ! is_mp; then
	log_unsupported "This test requires a multi-processor system."
fi

log_must mkdir -p ${TESTDIR}/tmp

typeset -i i=0
while [ $i -lt $NUMFILES ]; do
        (( i = i + 1 ))
        touch ${TESTDIR}/tmp/x$i > /dev/null 2>&1
done

sleep 3

rm -f ${TESTDIR}/tmp/x* >/dev/null 2>&1

rm_lnkcnt_zero_file ${TESTDIR}/tmp/test$$ > /dev/null 2>&1 &
PID=$!
log_note "rm_lnkcnt_zero_file ${TESTDIR}/tmp/test$$ pid: $PID"

i=0
while [ $i -lt $ITERS ]; do
	if ! pgrep rm_lnkcnt_zero_file > /dev/null ; then
		log_note "rm_lnkcnt_zero_file completes"
		break
	fi
	log_must sleep 10
	(( i = i + 1 ))
done

if pgrep rm_lnkcnt_zero_file > /dev/null; then
	log_must kill -TERM $PID
	log_fail "file link count is zero"
fi

log_must kill -TERM $PID
log_must rm -f ${TESTDIR}/tmp/test$$*

log_pass "Verify file link count is zero on zfs"
