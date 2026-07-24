#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or https://opensource.org/licenses/CDDL-1.0.
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

#
# Copyright (c) 2026 by Michael Heller. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/include/math.shlib

#
# DESCRIPTION:
# A POSIX_FADV_WILLNEED hint over a file much larger than the prefetch budget
# must not pin more than the budget's worth of outstanding prefetch.  This is
# the #15776 runaway: explicit user prefetch had no in-flight cap and could
# grow zio/ABD memory without bound until the system OOMed.  The cap is
# MAX(arc_c / 4, arc_c_max >> 4); with a small pinned ARC that is arc_c / 4.
#
# STRATEGY:
# 1. Pin the ARC small (arc_min < arc_max) so arc_c_max is small and known; the
#    budget can never exceed arc_c_max/4 (arc_c <= arc_c_max).
# 2. Create a file several times larger than that budget.
# 3. Inject a slow vdev so issued prefetch stays outstanding long enough to
#    observe, and export/import so the hint actually issues reads.
# 4. Storm POSIX_FADV_WILLNEED over the file while sampling the outstanding
#    explicit-prefetch bytes (the dmustats kstat).
# 5. Assert the peak is > 0 (the path was exercised) and within the budget.
# 6. Assert it drains back to 0 (no accounting leak).
#

verify_runnable "global"

FILE=$TESTDIR/$TESTFILE0
DISK=${DISKS%% *}
# arc_min must be strictly below arc_max (and arc_max >= MIN_ARC_MAX == 64M),
# otherwise the arc_max tuning is ignored.
ARC_MIN_BYTES=$((64 * 1024 * 1024))
ARC_MAX_BYTES=$((256 * 1024 * 1024))

function cleanup
{
	zinject -c all > /dev/null 2>&1
	restore_tunable ARC_MAX
	restore_tunable ARC_MIN
	[[ -e $FILE ]] && log_must rm -f $FILE
}

log_assert "Explicit WILLNEED prefetch is bounded to a fraction of the ARC (#15776)"
log_onexit cleanup

save_tunable ARC_MAX
save_tunable ARC_MIN
log_must set_tunable64 ARC_MIN $ARC_MIN_BYTES
log_must set_tunable64 ARC_MAX $ARC_MAX_BYTES

# The kernel budget is MAX(arc_c / 4, arc_c_max >> 4).  Since arc_c <= arc_c_max
# at all times, the budget can never exceed arc_c_max / 4 -- a stable ceiling we
# can assert against without racing arc_c growth during the storm.
typeset -i arc_cmax=$(kstat arcstats.c_max)
typeset -i budget=$((arc_cmax / 4))
log_note "arc_c_max=$arc_cmax -> prefetch budget ceiling=$budget bytes"

# A file several times the budget, so a single hint far exceeds it.
typeset -i fsize=$((budget * 4))
log_must file_write -o create -f $FILE -b 1048576 -c $((fsize / 1048576))
sync_pool $TESTPOOL

# Cold the cache so WILLNEED issues reads, then slow the vdev so the issued
# prefetch stays outstanding while we sample it.
log_must zpool export $TESTPOOL
log_must zpool import -d /dev $TESTPOOL
log_must zinject -d $DISK -D 20:1 $TESTPOOL

# Storm WILLNEED and track the peak outstanding explicit-prefetch bytes.
typeset -i peak=0 cur
typeset -i deadline=$((SECONDS + 5))
while (( SECONDS < deadline )); do
	file_fadvise -f $FILE -a POSIX_FADV_WILLNEED
	cur=$(kstat dmustats.prefetch_bytes_active)
	(( cur > peak )) && peak=$cur
done
log_note "peak outstanding explicit prefetch = $peak bytes (budget $budget)"

# The path must have been exercised, and must have stayed within the budget
# (plus a small margin for the one-block-per-caller reserve overshoot).
log_must test $peak -gt 0
typeset -i ceil=$((budget + budget / 10 + 1048576))
log_must test $peak -le $ceil

# Clear the fault and confirm the accounting drains back to zero (no leak).
log_must zinject -c all
typeset -i i=0
while (( $(kstat dmustats.prefetch_bytes_active) != 0 && i < 60 )); do
	sleep 1
	(( i += 1 ))
done
log_must test $(kstat dmustats.prefetch_bytes_active) -eq 0

log_pass "WILLNEED prefetch stayed within the ARC budget and drained to zero"
