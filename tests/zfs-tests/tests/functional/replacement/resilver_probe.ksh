#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source. A copy of the CDDL is also available via the Internet at
# https://opensource.org/license/CDDL-1.0.
#

# shellcheck disable=SC1091
. "$STF_SUITE"/include/libtest.shlib

#
# DESCRIPTION:
#	Recovery from a failed probe invalidates healing coverage even when
#	the device's enum state has not changed, and a pass that completes
#	while such devices stay unwritable keeps their missing ranges.
#
# STRATEGY:
#	Use libzpool to hold the async worker while exercising real probes,
#	scan setup, reopen/clear/removal checks, and persistence on file vdevs.
#	Reopen two returning leaves together to check parallel child accounting.
#	Complete a pass without them, then require healing after they return.
#	Export and import a pass whose saved state matches its healing copy,
#	lacks it, or has advanced past it; only the first may resume.
#

verify_runnable "global"

function cleanup
{
	rm -rf "$workdir"
}

log_assert "Probe recovery accounts for writeability independently of enum state"
workdir=$(mktemp -d "$TEST_BASE_DIR/resilver_probe.XXXXXX") ||
    log_fail "cannot create test directory"
log_onexit cleanup
# Each invocation owns a userspace pool and holds its async worker to keep probe
# recovery ahead of the fault-state transition.
for mode in reopen clear probe complete resume legacy advanced; do
	log_must resilver_probe "$workdir" "$mode"
done
log_pass "Probe recovery and import accounted for healing coverage"
