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
