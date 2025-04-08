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
# Copyright (c) 2018 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_wait/zpool_wait.kshlib

#
# DESCRIPTION:
# 'zpool wait' returns immediately when there is no activity in progress.
#
# STRATEGY:
# 1. Create an empty pool with no activity
# 2. Run zpool wait with various activities, make sure it always returns
#    promptly
#

function cleanup {
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
}

typeset -r TIMEOUT_SECS=1

log_onexit cleanup
log_must zpool create $TESTPOOL $DISK1

# Wait for each activity
typeset activities=(free discard initialize replace remove resilver scrub)
for activity in ${activities[@]}; do
	log_must timeout $TIMEOUT_SECS zpool wait -t $activity $TESTPOOL
done

# Wait for multiple activities at the same time
log_must timeout $TIMEOUT_SECS zpool wait -t scrub,initialize $TESTPOOL
log_must timeout $TIMEOUT_SECS zpool wait -t free,remove,discard $TESTPOOL

# Wait for all activities at the same time
log_must timeout $TIMEOUT_SECS zpool wait $TESTPOOL

log_pass "'zpool wait' returns immediately when no activity is in progress."
