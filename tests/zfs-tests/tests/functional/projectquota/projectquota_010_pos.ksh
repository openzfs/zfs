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
# Copyright (c) 2026 by the OpenZFS project.  All rights reserved.
#

. $STF_SUITE/tests/functional/projectquota/projectquota_common.kshlib

#
# DESCRIPTION:
#	Removing many projects at once shrinks the project usage ZAP
#	while the pool syncs. That sync must not panic.
#
# STRATEGY:
#	1. Create 2000 directories, each with its own project ID and a
#	   one-line file, and sync the pool.
#	2. Verify zfs projectspace lists all 2000 projects.
#	3. Remove the directories and sync the pool.
#	4. Verify the pool is still healthy.
#

verify_runnable "both"

function cleanup
{
	log_must cleanup_projectquota
}

log_onexit cleanup

log_assert "Removing many projects at once does not panic"

log_must mkdir $PRJDIR
for (( i = 10001; i <= 12000; i++ )); do
	log_must mkdir $PRJDIR/$i
	log_must zfs project -s -p $i $PRJDIR/$i
	log_must eval "echo $i > $PRJDIR/$i/file"
done
sync_pool

count=$(zfs projectspace -H -o name $QFS | \
    awk '$1 >= 10001 && $1 <= 12000' | wc -l)
(( count == 2000 )) || log_fail "expected 2000 projects, found $count"

log_must rm -rf $PRJDIR
sync_pool
log_must check_state $TESTPOOL "" "ONLINE"

log_pass "Removing many projects at once does not panic"
