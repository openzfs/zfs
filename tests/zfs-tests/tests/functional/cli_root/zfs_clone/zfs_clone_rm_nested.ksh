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
# http://www.illumos.org/license/CDDL.
#

#
# Copyright (c) 2017 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	When a snapshot is destroyed, we used to recurse all clones
#	that are downstream of the destroyed snapshot (e.g. to remove
#	its key and merge its deadlist entries to the previous one).
#	This recursion would break the stack on deeply nested clone
#	hierarchies. To avoid this problem today, we keep heap-allocated
#	records of all the clones as we traverse their hierarchy.
#
#	This test ensures and showcases that our new method works with
#	deeply nested clone hierarchies.
#
# STRATEGY:
#	1. Create an fs and take a snapshot of it (snapshot foo)
#	2. Take a second snapshot of the same fs (snapshot bar) on
#	   top of snapshot foo
#	3. Create a clone of snapshot bar and then take a snapshot
#	   of it.
#	4. Create a clone of the newly-created snapshot and then
#	   take a snapshot of it.
#	5. Repeat step [4] many times to create a deeply nested hierarchy.
#	6. Destroy snapshot foo.
#

verify_runnable "both"

typeset FS0=$TESTPOOL/0
typeset FOO=foo
typeset BAR=BAR

typeset FS0SNAPFOO=$FS0@$FOO
typeset FS0SNAPBAR=$FS0@$BAR

typeset -i numds=300

log_must zfs create $FS0

function test_cleanup
{
	log_must zfs destroy -Rf $FS0

	return 0
}

log_must zfs snapshot $FS0SNAPFOO
log_must zfs snapshot $FS0SNAPBAR

log_onexit test_cleanup

for (( i=1; i<numds; i++ )); do
	log_must zfs clone $TESTPOOL/$((i-1))@$BAR $TESTPOOL/$i
	log_must zfs snapshot $TESTPOOL/$i@$BAR
done

log_must zfs destroy $FS0SNAPFOO

log_pass "Snapshot deletion doesn't break the stack in deeply nested " \
    "clone hierarchies."
