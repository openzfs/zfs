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
# Copyright (c) 2026, Patrick Domack. All rights reserved.
#

. $STF_SUITE/tests/functional/clonedup/clonedup.kshlib
. $STF_SUITE/tests/functional/removal/removal.kshlib

#
# DESCRIPTION:
#	A block on a device that is being removed, or on the indirect
#	device that a finished removal leaves, never survives, but a
#	run can redirect it onto a copy elsewhere.  ZFS never remaps
#	a cloned block pointer, so a survivor there would keep the
#	indirect mapping for good.  When every copy is on such a
#	device, the run redirects none of them.
#
# STRATEGY:
#	1. Write a, then x and its copy x2, on the first disk.  Add a
#	   second disk and start removing the first, with the removal
#	   held, then copy a to b, which lands on the second disk.
#	2. Run.  a is the older copy, yet it moves onto b, and b keeps
#	   its blocks.  x and x2 stay apart.
#	3. Let the removal finish, so x and x2 are on the indirect
#	   device.  A full run still leaves them apart.
#	4. Copy x to x3, on the second disk, and run.  The match walk
#	   finds x and x2 as older copies and takes neither as the
#	   source, so x3 stays apart.
#	5. A full run moves x and x2 onto x3, and x3 keeps its
#	   blocks.  Every file reads back as written, the pool scrubs
#	   clean and zdb finds no leak.
#

verify_runnable "global"

function cleanup
{
	log_must set_tunable32 REMOVAL_SUSPEND_PROGRESS 0
	clonedup_cleanup
}

# The vdev of each of a file's blocks, one per line.
function rv_vdevs # ds file
{
	clonedup_file_blocks $1 $2 |
	    awk '{ split($2, d, ":"); print d[1] }'
}

log_assert "a block on a removed device never survives"
log_onexit cleanup

set -A disks $DISKS
(( ${#disks[@]} >= 2 )) || log_unsupported "needs two disks"
typeset all="$(clonedup_all_blocks)"
typeset first keep v

log_must zpool create -f -o feature@clonedup=enabled \
    -O compression=off $TESTPOOL ${disks[0]}
clonedup_write /$TESTPOOL/a
clonedup_write /$TESTPOOL/x
clonedup_dup /$TESTPOOL/x /$TESTPOOL/x2
clonedup_sync
first=$(rv_vdevs $TESTPOOL a | sort -u)
log_must zpool add -f $TESTPOOL ${disks[1]}
log_must set_tunable32 REMOVAL_SUSPEND_PROGRESS 1
log_must zpool remove $TESTPOOL ${disks[0]}
clonedup_dup /$TESTPOOL/a /$TESTPOOL/b
clonedup_sync
for v in $(rv_vdevs $TESTPOOL b); do
	log_must [ "$v" != "$first" ]
done
keep=$(clonedup_file_blocks $TESTPOOL b)

# a is older, but it is on the removing disk
clonedup_run
log_must [ "$(clonedup_file_blocks $TESTPOOL b)" = "$keep" ]
clonedup_check_shared $TESTPOOL a $TESTPOOL b "$all"
clonedup_check_shared $TESTPOOL x $TESTPOOL x2 ""
clonedup_jstat_is clonedup_applied $CD_BLOCKS
clonedup_jstat_is errors 0

log_must set_tunable32 REMOVAL_SUSPEND_PROGRESS 0
log_must wait_for_removal $TESTPOOL

# x and x2 are on the indirect device and have no survivor
clonedup_run -f
clonedup_check_shared $TESTPOOL x $TESTPOOL x2 ""

clonedup_dup /$TESTPOOL/x /$TESTPOOL/x3
clonedup_sync
keep=$(clonedup_file_blocks $TESTPOOL x3)
clonedup_run
clonedup_check_shared $TESTPOOL x $TESTPOOL x3 ""
clonedup_check_shared $TESTPOOL x2 $TESTPOOL x3 ""

clonedup_run -f
log_must [ "$(clonedup_file_blocks $TESTPOOL x3)" = "$keep" ]
clonedup_check_shared $TESTPOOL x $TESTPOOL x3 "$all"
clonedup_check_shared $TESTPOOL x2 $TESTPOOL x3 "$all"
clonedup_jstat_is errors 0
log_must cmp /$TESTPOOL/a /$TESTPOOL/b
log_must cmp /$TESTPOOL/x /$TESTPOOL/x3
log_must cmp /$TESTPOOL/x2 /$TESTPOOL/x3
log_must zpool scrub -w $TESTPOOL
log_must eval "zpool status $TESTPOOL |" \
    "grep -q 'No known data errors'"
clonedup_leakcheck

log_pass "a block on a removed device never survives"
