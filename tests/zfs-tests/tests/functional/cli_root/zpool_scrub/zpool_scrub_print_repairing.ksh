#!/bin/ksh -p
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
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
