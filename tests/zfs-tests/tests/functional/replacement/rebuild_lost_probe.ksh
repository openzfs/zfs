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
#	A sequential rebuild whose reads fail because its whole mirror became
#	unavailable resumes at the first lost segment, and the new device
#	alone holds the data once it completes. It restarts by itself when
#	the leaves return while it is still stopping.
#
# STRATEGY:
#	Use libzpool, with failmode=continue, to hold a rebuild's reads in the
#	source's queue, lose both leaves, fail a sync's writes to the mirror,
#	then release the reads. The rebuild must stop without saving progress
#	past the lost segment and complete after the leaves return and the
#	pool is imported again. Repeat, but return the leaves while the stopping
#	rebuild waits for a held txg and handle their resilver request then;
#	the rebuild must restart without an import. Import each result
#	without the source and scrub it.
#

verify_runnable "global"

POOL=rebuild_lost

function cleanup
{
	poolexists $POOL && destroy_pool $POOL
	rm -rf "$workdir"
}

log_assert "A rebuild resumes at the segments it lost to an unavailable vdev"
workdir=$(mktemp -d "$TEST_BASE_DIR/rebuild_lost_probe.XXXXXX") ||
    log_fail "cannot create test directory"
log_onexit cleanup
for mode in import return; do
	log_must rebuild_lost_probe "$workdir" "$mode"
	log_must rm "$workdir/disk-1"
	log_must zpool import -N -d "$workdir" $POOL
	log_must zpool wait -t resilver $POOL
	log_must zpool scrub -w $POOL
	log_must eval "zpool status $POOL | grep -q 'repaired 0B .* 0 errors'"
	log_must zpool destroy $POOL
done
log_pass "A rebuild resumed at the segments it lost to an unavailable vdev"
