#! /bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# CDDL HEADER START
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
# CDDL HEADER END
#

#
# Copyright (c) 2019 by Delphix. All rights reserved.
# Copyright (c) 2025, Klara, Inc.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:

# This tests the on-demand "flush all spacemap logs" feature. This is the same
# process is that triggered at pool export, but instead we trigger it ahead of
# time via `zpool condense`.
#
# This test uses the `log_spacemaps` kstat and `zdb -m` to know how much is
# waiting to be flushed. All we're looking for is that the flushall function
# works, not how much it's doing.

#
# STRATEGY:
#	1. Create pool.
#	2. Write things, which will add to the spacemap logs.
#	3. Save the counters.
#	4. Request the spacemap logs be flushed.
#	5. Compare counters against previous values.
#

verify_runnable "global"

function cleanup
{
	if poolexists $LOGSM_POOL; then
		log_must zpool destroy -f $LOGSM_POOL
	fi
}
log_onexit cleanup

function get_smp_length {
	zdb -m $LOGSM_POOL | grep smp_length | \
	    awk '{ sum += $3 } END { print sum }'
}

LOGSM_POOL="logsm_flushall"
read -r TESTDISK _ <<<"$DISKS"

log_must zpool create -o cachefile=none -f -O compression=off \
    $LOGSM_POOL $TESTDISK

log_must file_write -o create -f /$LOGSM_POOL/f1 -b 131072 -c 32 -d R
log_must file_write -o create -f /$LOGSM_POOL/f2 -b 131072 -c 32 -d R
log_must file_write -o create -f /$LOGSM_POOL/f3 -b 131072 -c 32 -d R
log_must file_write -o create -f /$LOGSM_POOL/f4 -b 131072 -c 32 -d R

sync_all_pools

typeset length_1=$(get_smp_length)

log_must zpool condense -t log-spacemap -w $LOGSM_POOL

typeset length_2=$(get_smp_length)

log_must test $length_1 -gt $length_2

log_pass "Log spacemaps on-demand flushall works"
