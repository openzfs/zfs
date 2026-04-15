#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/block_cloning/block_cloning.kshlib

#
# DESCRIPTION:
#	When a block is truncated and then cloned to, a read data corruption can occur.
#	This is a regression test for #18412.
#

verify_runnable "global"

claim="No read data corruption when cloning blocks after a truncate"

function cleanup
{
	datasetexists $TESTPOOL && destroy_pool $TESTPOOL
}

log_onexit cleanup

log_must zpool create -o feature@block_cloning=enabled $TESTPOOL $DISKS

# Run for a few times to increase the likelihood of bug triggering.
for i in {0..50}; do
	log_must clone_after_trunc /$TESTPOOL/
done

log_pass $claim
