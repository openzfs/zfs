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
# Copyright (c) 2019 by Tim Chase. All rights reserved.
# Copyright (c) 2019 Lawrence Livermore National Security, LLC.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_trim/zpool_trim.kshlib

#
# DESCRIPTION:
#	Verify 'zpool trim -r <rate>' rate limiting.
#
# STRATEGY:
#	1. Create a pool on a single disk.
#	2. Manually TRIM the pool with rate limiting.
#	3. Verify the TRIM can be suspended.
#	4. Restart the TRIM and verify the rate is preserved.
#
# NOTE: Rate limiting caps the maximum trim rate; a busy system can trim
# more slowly. Check the minimum elapsed time to reach a given progress,
# rather than requiring a fixed amount of progress after a short sleep.
#

function cleanup
{
	if poolexists $TESTPOOL; then
		destroy_pool $TESTPOOL
	fi

	if [[ -d "$TESTDIR" ]]; then
		rm -rf "$TESTDIR"
	fi
}
log_onexit cleanup

function trim_to_progress # target rate [resume]
{
	typeset -i target=$1
	typeset -i rate=$2
	typeset -i progress
	typeset -i minimum
	typeset start
	typeset -i elapsed
	typeset -i i

	progress=$(trim_progress $TESTPOOL $LARGEFILE)
	(( minimum = (target - progress) * 10240 / 100 / rate ))
	start=$(date +%s)

	if [[ "$3" == resume ]]; then
		log_must zpool trim $TESTPOOL
	else
		log_must zpool trim -r ${rate}M $TESTPOOL
	fi

	for (( i = 0; i < 60; i++ )); do
		progress=$(trim_progress $TESTPOOL $LARGEFILE)
		if (( progress >= target )); then
			(( target < 100 )) && break
			trim_prog_line $TESTPOOL $LARGEFILE | \
			    grep -q complete && break
		fi
		log_must sleep 1
	done
	(( progress >= target )) || log_fail \
	    "TRIM did not reach $target%: $(trim_prog_line $TESTPOOL $LARGEFILE)"

	(( elapsed = $(date +%s) - start ))
	# Allow one second for timestamp rounding and percentage truncation.
	log_note "TRIM reached $progress% in ${elapsed}s at a ${rate}M/s limit"
	(( elapsed >= minimum - 1 )) || log_fail "TRIM exceeded rate limit"

	if (( target < 100 )); then
		log_must zpool trim -s $TESTPOOL
		log_must eval "trim_prog_line $TESTPOOL $LARGEFILE | grep suspended"
	else
		log_must eval "trim_prog_line $TESTPOOL $LARGEFILE | grep complete"
	fi
}

LARGEFILE="$TESTDIR/largefile"

log_must mkdir "$TESTDIR"
log_must truncate -s 10G "$LARGEFILE"
log_must zpool create -f $TESTPOOL "$LARGEFILE"

# Start at 200M/s, then resume without specifying a new rate.
trim_to_progress 10 200
trim_to_progress 20 200 resume

# Increase the rate and finally allow trimming at the maximum rate.
trim_to_progress 50 600
trim_to_progress 100 1048576

log_pass "Manual TRIM rate throttles as expected"
