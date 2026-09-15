#!/bin/ksh
# SPDX-License-Identifier: CDDL-1.0

#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License (CDDL), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the CDDL should have accompanied this source. A copy is also
# available at https://opensource.org/license/CDDL-1.0.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/perf/perf.shlib

verify_runnable "both"

typeset available=1000000
for comp_percent in 0 10 66 100; do
	typeset size=$(get_zstd_workload_size "$available" "$comp_percent")
	log_must test "$size" -eq 500000
done

log_mustnot get_zstd_workload_size "$available" -1
log_mustnot get_zstd_workload_size "$available" 101

log_pass "Zstd workload sizing is independent of compression percentage"
