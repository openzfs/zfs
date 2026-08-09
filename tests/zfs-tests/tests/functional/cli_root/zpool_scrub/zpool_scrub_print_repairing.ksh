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
# Copyright (c) 2018 Lawrence Livermore National Security, LLC.

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_scrub/zpool_scrub.cfg

#
# DESCRIPTION:
#	zpool status should print "(repairing)" on drives with errors found
#	while scrubbing.
#
# STRATEGY:
#	1. Create a file (already done in setup.ksh)
#	2. Inject read errors on one vdev
#	3. Run a scrub
#	4. Verify we see "(repairing)" on the bad vdev
#

verify_runnable "global"

log_assert "Verify we see '(repairing)' while scrubbing a bad vdev."

function cleanup
{
	log_must zinject -c all
	log_must set_tunable64 SCAN_VDEV_LIMIT $ZFS_SCAN_VDEV_LIMIT_DEFAULT
	zpool scrub -s $TESTPOOL || true
}

log_onexit cleanup

# A file is already created in setup.ksh.  Inject read errors on the first disk.
log_must zinject -d $DISK1 -e io -T read -f 100 $TESTPOOL

# Make the scrub slow
log_must zinject -d $DISK1 -D10:1 $TESTPOOL
log_must set_tunable64 SCAN_VDEV_LIMIT $ZFS_SCAN_VDEV_LIMIT_SLOW

log_must zpool scrub $TESTPOOL

# Wait for the scrub to show '(repairing)'.  Timeout after 10 sec if it doesn't
# show it.
for i in {0..100} ; do
	if ! is_pool_scrubbing $TESTPOOL ; then
		break
	fi

	if zpool status | grep "$DISK1" | grep -q '(repairing)' ; then
		log_pass "Correctly saw '(repairing)' while scrubbing"
	fi

	sleep 0.1
done
log_fail "Never saw '(repairing)' while scrubbing"
