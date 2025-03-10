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
# Copyright 2019, loli10K <ezomori.nozomu@gmail.com>. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/perf/perf.shlib

function cleanup
{
	# Set tunables to their recorded actual size and then to their original
	# value: this works for previously unconfigured tunables.
	log_must set_tunable64 ARC_MIN "$MINSIZE"
	log_must set_tunable64 ARC_MIN "$ZFS_ARC_MIN"
	log_must set_tunable64 ARC_MAX "$MAXSIZE"
	log_must set_tunable64 ARC_MAX "$ZFS_ARC_MAX"
}

log_onexit cleanup

ZFS_ARC_MAX="$(get_tunable ARC_MAX)"
ZFS_ARC_MIN="$(get_tunable ARC_MIN)"
MINSIZE="$(get_min_arc_size)"
MAXSIZE="$(get_max_arc_size)"

log_assert "ARC tunables should be updated dynamically"

for size in $((MAXSIZE/4)) $((MAXSIZE/3)) $((MAXSIZE/2)) $MAXSIZE; do
	log_must set_tunable64 ARC_MAX "$size"
	log_must test "$(get_max_arc_size)" == "$size"
	log_must set_tunable64 ARC_MIN "$size"
	log_must test "$(get_min_arc_size)" == "$size"
done

log_pass "ARC tunables can be updated dynamically"
