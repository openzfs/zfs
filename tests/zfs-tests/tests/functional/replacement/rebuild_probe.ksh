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

. "$STF_SUITE"/include/libtest.shlib

#
# DESCRIPTION:
#	An imported rebuild resumes with its error count only while its saved
#	progress matches the copy kept by software which counts failed writes.
#
# STRATEGY:
#	Use libzpool to hold a sequential rebuild and advance its saved
#	progress: with an error count and the matching copy, with the count
#	but without the copy, or past the copy alone. Export and import the
#	pool. Only the first may resume with its saved offset and count; the
#	others must reset both.
#	Also remove the copy from a rebuild whose new device's DTL starts at
#	txg 0, as older rebuilds could leave it. The reset rebuild must
#	complete.
#

verify_runnable "global"

function cleanup
{
	rm -rf "$workdir"
}

log_assert "Imported rebuild progress is trusted only with its matching copy"
workdir=$(mktemp -d "$TEST_BASE_DIR/rebuild_probe.XXXXXX") ||
    log_fail "cannot create test directory"
log_onexit cleanup
for mode in resume legacy advanced zero; do
	log_must rebuild_probe "$workdir" "$mode"
done
log_pass "Imported rebuild progress was trusted only with its matching copy"
