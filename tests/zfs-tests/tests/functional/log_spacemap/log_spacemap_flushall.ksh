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
# Copyright (c) 2025-2026, Klara, Inc.
# Copyright (c) 2026, TrueNAS.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:

# This tests the on-demand "flush all spacemap logs" feature. This is the same
# process is that triggered at pool export, but instead we trigger it ahead of
# time via `zpool condense`.
#
# This test uses `zdb -m` to know how much is waiting to be flushed. All we're
# looking for is that the flushall function works, not how much it's doing.

#
# STRATEGY:
#	1. Create pool.
#	2. Write things, which will add to the spacemap logs.
#	3. Save the counters.
#	4. Request the spacemap logs be flushed.
#	5. Compare counters against previous values.
#

verify_runnable "global"

LOGSM_POOL="logsm_flushall"

function cleanup
{
	if poolexists $LOGSM_POOL; then
		log_must zpool destroy -f $LOGSM_POOL
	fi
}
log_onexit cleanup

function get_smp_length {
	#
	# zdb -m output includes:
	#
	#   Log Space Maps in Pool:
	#   Log Spacemap object 152330 txg 4493989
	#   space map object 152330:
	#     smp_length = 0x670
	#     smp_alloc = 0x0
	#   Log Spacemap object 151720 txg 4493990
	#     space map object 151720:
	#   ...
	#
	# Traditional awk doesn't understand hex numbers, so we run them back
	# through printf to covert to decimal before summing them.
	#
	zdb -m $LOGSM_POOL | awk '
	    /^Log Spacemap object/ { onoff = 1 }
	    /smp_length/ && onoff { print $3 }
	' | xargs -n1 printf '%d\n' | awk '
	    { sum += $1 } END { print sum }
	'
}

log_must zpool create -o cachefile=none -f \
    -O compression=off -O recordsize=16k \
    $LOGSM_POOL $DISKS

for s in $(seq 256) ; do
	log_must file_write -o create -f /$LOGSM_POOL/$s -b 16384 -c 1 -d R
	sync_pool $LOGSM_POOL
done

typeset length_1=$(get_smp_length)

log_must zpool condense -t log-spacemap -w $LOGSM_POOL

typeset length_2=$(get_smp_length)

log_must test $length_1 -gt $length_2

log_pass "Log spacemaps on-demand flushall works"
