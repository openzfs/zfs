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
#	Verify scrub -e, -p, and -s show the right status.
#
# STRATEGY:
#	1. Create a pool and create a 10MB file in it (using indirect blocks).
#	2. Inject write errors on the file.
#	3. Start a file scrub.
#	4. Verify that the file was reported as a buggy.
#

verify_runnable "global"

function cleanup
{
	log_must zinject -c all
	rm -f /$TESTPOOL/1g_file
	log_must zpool clear $TESTPOOL
}

log_onexit cleanup

log_assert "Verify large file scrub with indirect blocks"

# To automatically determine the pool in which a file resides, access to the
# list of pools is required.
unset __ZFS_POOL_EXCLUDE
export __ZFS_POOL_RESTRICT="$TESTPOOL"

log_must fio --rw=write --name=job --size=1G --filename=/$TESTPOOL/1g_file

log_must sync_pool $TESTPOOL

log_must zinject -t data -e checksum -f 100 -am /$TESTPOOL/1g_file

log_must is_pool_without_errors $TESTPOOL true
log_must zfs scrub /$TESTPOOL/1g_file
log_must zpool wait -t scrub $TESTPOOL
log_mustnot is_pool_without_errors $TESTPOOL true

log_pass "Verified file scrub shows expected status."
