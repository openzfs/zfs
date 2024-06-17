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
# Copyright 2024 Klara, Inc.
# Copyright 2024 Mariusz Zaborski <oshogbo@FreeBSD.org>
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	Scrub a single file and self-healing using additional copies.
#
# STRATEGY:
#	1. Create a dataset with copies=3
#	2. Write a file to the dataset
#	3. zinject errors into the first and second DVAs of that file
#	4. Scrub single file and verify the scrub repaired all errors
#	5. Remove the zinject handler
#

verify_runnable "global"

function cleanup
{
	log_must zinject -c all
	destroy_dataset $TESTPOOL/$TESTFS2
	log_must zpool clear $TESTPOOL
}

log_onexit cleanup

log_assert "Verify scrubing a single file with additional copies"

# To automatically determine the pool in which a file resides, access to the
# list of pools is required.
unset __ZFS_POOL_EXCLUDE
export __ZFS_POOL_RESTRICT="$TESTPOOL"

log_must zfs create -o copies=3 $TESTPOOL/$TESTFS2
typeset mntpnt=$(get_prop mountpoint $TESTPOOL/$TESTFS2)

log_must fio --rw=write --name=job --size=10M --filename=$mntpnt/file
log_must sync_pool $TESTPOOL

log_must zinject -a -t data -C 0,1 -e io $mntpnt/file

log_must zfs scrub $mntpnt/file
log_must is_pool_without_errors $TESTPOOL true

log_must fio --rw=write --name=job --size=10M --filename=$mntpnt/file
log_must is_pool_without_errors $TESTPOOL true

log_pass "Verified file scrub shows expected status."
