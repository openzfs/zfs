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
#	Namespace operations on a dataset with changelog=on are recorded
#	and read back by a registered consumer.
#
# STRATEGY:
#	1. Use synchronous mode so records land with the operation.
#	2. Create a dataset with changelog=on and register a consumer.
#	3. Create, mkdir, rename and remove entries.
#	4. Verify a record for each operation is read back.
#

verify_runnable "global"

function cleanup
{
	log_must set_tunable32 CHANGELOG_ASYNC $saved_async
	datasetexists $CLFS && destroy_dataset $CLFS -r
}

saved_async=$(get_tunable CHANGELOG_ASYNC)
log_onexit cleanup

log_assert "Namespace operations are recorded in the changelog"

log_must set_tunable32 CHANGELOG_ASYNC 0
log_must zfs create -o changelog=on $CLFS
mntpnt=$(get_prop mountpoint $CLFS)
log_must zchangelog register $CLFS consumer1

log_must touch $mntpnt/a
log_must mkdir $mntpnt/d
log_must mv $mntpnt/a $mntpnt/b
log_must rm $mntpnt/b

out=$(zchangelog read $CLFS -c consumer1 -n 64) ||
    log_fail "zchangelog read failed"
for op in CREATE MKDIR RENAME REMOVE; do
	echo "$out" | grep -qw $op || log_fail "$op record missing"
done

log_must zchangelog unregister $CLFS consumer1

log_pass "Namespace operations are recorded in the changelog"
