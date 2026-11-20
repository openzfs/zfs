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
# Copyright (c) 2026 by HeonJe Lee
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/changelog/changelog.cfg

#
# DESCRIPTION:
#	Clearing acknowledged records advances the tail, and a consumer
#	registered later starts at the current head.
#
# STRATEGY:
#	1. Use synchronous mode so records land with the operation.
#	2. Register a consumer and create a few files.
#	3. Read with -a to acknowledge everything read.
#	4. Verify the tail reached the head.
#	5. Register a second consumer and verify it starts at the head.
#

verify_runnable "global"

function cleanup
{
	log_must set_tunable32 CHANGELOG_ASYNC $saved_async
	datasetexists $CLFS && destroy_dataset $CLFS -r
}

saved_async=$(get_tunable CHANGELOG_ASYNC)
log_onexit cleanup

log_assert "Clearing records advances the tail"

log_must set_tunable32 CHANGELOG_ASYNC 0
log_must zfs create -o changelog=on $CLFS
mntpnt=$(get_prop mountpoint $CLFS)
log_must zchangelog register $CLFS consumer2

for i in 1 2 3 4 5; do
	log_must touch $mntpnt/f$i
done

log_must eval "zchangelog read $CLFS -c consumer2 -n 64 -a >/dev/null"

head=$(zchangelog status $CLFS | awk '{print $2}')
tail=$(zchangelog status $CLFS | awk '{print $4}')
(( head >= 5 )) || log_fail "head is $head, expected at least 5"
(( tail == head )) || log_fail "tail $tail did not reach head $head"

seq=$(zchangelog register $CLFS consumer3 | awk '{print $NF}')
(( seq == head )) || log_fail "new consumer starts at $seq, not $head"

log_pass "Clearing records advances the tail"
