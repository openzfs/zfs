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

# Detect and make sure this test must be executed on a multi-process system
is_mp || log_fail "This test requires a multi-processor system."

log_must $MKDIR -p ${TESTDIR}/tmp

typeset -i i=0
while [ $i -lt $NUMFILES ]; do
        (( i = i + 1 ))
        $TOUCH ${TESTDIR}/tmp/x$i > /dev/null 2>&1
done

sleep 3

$RM -f ${TESTDIR}/tmp/x* >/dev/null 2>&1

$RM_LNKCNT_ZERO_FILE ${TESTDIR}/tmp/test$$ > /dev/null 2>&1 &
PID=$!
log_note "$RM_LNKCNT_ZERO_FILE ${TESTDIR}/tmp/test$$ pid: $PID"

i=0
while [ $i -lt $ITERS ]; do
	if ! $PGREP $RM_LNKCNT_ZERO_FILE > /dev/null ; then
		log_note "$RM_LNKCNT_ZERO_FILE completes"
		break
	fi
	log_must $SLEEP 10
	(( i = i + 1 ))
done

if $PGREP $RM_LNKCNT_ZERO_FILE > /dev/null; then
	log_must $KILL -TERM $PID
	log_fail "file link count is zero"
fi

log_must $KILL -TERM $PID
log_must $RM -f ${TESTDIR}/tmp/test$$*

log_pass "Verify file link count is zero on zfs"
