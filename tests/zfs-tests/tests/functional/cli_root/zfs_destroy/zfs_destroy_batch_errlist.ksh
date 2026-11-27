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
# Copyright (c) 2026 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	lzc_destroy_snaps() is all-or-nothing: if any snapshot in the batch
#	fails its check, no snapshot in the batch is destroyed, and every
#	snapshot that failed is reported.  A snapshot that no longer exists
#	is silently ignored and does not stop the rest of the batch.
#
# STRATEGY:
#	1. Create five snapshots and put a user hold on the second and the
#	   fourth, so their destroy check fails with EBUSY.
#	2. Destroy all five in one batch.  Verify it fails, that all five
#	   still exist, and that both held snapshots are named in the error
#	   output -- not just the first one to fail.
#	3. Release the holds, then destroy the five together with a name that
#	   does not exist.  Verify it succeeds and destroys the five.
#

verify_runnable "both"

typeset FS=$TESTPOOL/batcherr

function cleanup
{
	typeset snap tag rest
	if datasetexists $FS; then
		for snap in $(zfs list -H -t snapshot -o name -r $FS); do
			zfs holds -H $snap 2>/dev/null |
			    while read -r s tag rest; do
				zfs release $tag $s 2>/dev/null
			    done
		done
		destroy_dataset $FS -r
	fi
}

log_onexit cleanup
log_assert "A batch destroy with a failing snapshot destroys none of them" \
    "and reports all of them"

log_must zfs create $FS
typeset -i i=1
while (( i <= 5 )); do
	log_must zfs snapshot $FS@s$i
	(( i = i + 1 ))
done

log_must zfs hold keep $FS@s2
log_must zfs hold keep $FS@s4

typeset err
typeset -i rc
err=$(zfs destroy $FS@s1,s2,s3,s4,s5 2>&1)
rc=$?

log_note "zfs destroy exited $rc: $err"

(( rc == 0 )) && log_fail "batch destroy succeeded with two held snapshots"

# Nothing in the batch may have been destroyed.
i=1
while (( i <= 5 )); do
	log_must datasetexists $FS@s$i
	(( i = i + 1 ))
done

# Every snapshot that failed must be reported, not only the first.
echo "$err" | grep -q "$FS@s2" ||
    log_fail "the error output does not name $FS@s2: $err"
echo "$err" | grep -q "$FS@s4" ||
    log_fail "the error output does not name $FS@s4: $err"

log_must zfs release keep $FS@s2
log_must zfs release keep $FS@s4

# A snapshot that does not exist is ignored; the rest are destroyed.
log_must zfs destroy $FS@s1,s2,nosuchsnap,s3,s4,s5
i=1
while (( i <= 5 )); do
	log_mustnot datasetexists $FS@s$i
	(( i = i + 1 ))
done

log_pass "A batch destroy with a failing snapshot destroys none of them" \
    "and reports all of them"
