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
#	In asynchronous mode records are delivered after the txg sync
#	flush, and changelog=off stops new records.
#
# STRATEGY:
#	1. In asynchronous mode, create a file and wait for its record.
#	2. In synchronous mode, create a file with changelog=off and
#	   verify the head does not move.
#	3. Turn changelog=on, create a file and verify the head moves.
#

verify_runnable "global"

function cleanup
{
	log_must set_tunable32 CHANGELOG_ASYNC $saved_async
	datasetexists $CLFS && destroy_dataset $CLFS -r
	datasetexists ${CLFS}_gate && destroy_dataset ${CLFS}_gate -r
}

function head_of # dataset
{
	zchangelog status $1 | awk '{print $2}'
}

saved_async=$(get_tunable CHANGELOG_ASYNC)
log_onexit cleanup

log_assert "Async records are delivered and changelog=off gates new ones"

log_must set_tunable32 CHANGELOG_ASYNC 1
log_must zfs create -o changelog=on $CLFS
mntpnt=$(get_prop mountpoint $CLFS)
log_must zchangelog register $CLFS consumer4
log_must touch $mntpnt/g1

typeset -i i=0
while (( i < 5 )); do
	zchangelog read $CLFS -c consumer4 -n 64 -w 2000 | grep -qw CREATE &&
	    break
	sync_pool $TESTPOOL
	(( i += 1 ))
done
(( i < 5 )) || log_fail "async record was not delivered"

log_must set_tunable32 CHANGELOG_ASYNC 0
log_must zfs create -o changelog=off ${CLFS}_gate
mntpnt=$(get_prop mountpoint ${CLFS}_gate)
log_must zchangelog register ${CLFS}_gate consumer5

before=$(head_of ${CLFS}_gate)
log_must touch $mntpnt/g2
after=$(head_of ${CLFS}_gate)
(( after == before )) || log_fail "head moved with changelog=off"

log_must zfs set changelog=on ${CLFS}_gate
log_must touch $mntpnt/g3
after=$(head_of ${CLFS}_gate)
(( after > before )) || log_fail "head did not move with changelog=on"

log_pass "Async records are delivered and changelog=off gates new ones"
