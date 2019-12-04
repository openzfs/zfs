#!/bin/ksh -p
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
# 'zpool wait' behaves sensibly when invoked incorrectly.
#
# STRATEGY:
# 1. Invoke 'zpool wait' incorrectly and check that it exits with a non-zero
#    status.
# 2. Invoke 'zpool wait' with missing or bad arguments and check that it prints
#    some sensible error message.
#

function cleanup {
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
}

log_onexit cleanup
log_must zpool create $TESTPOOL $DISK1

log_mustnot zpool wait

zpool wait 2>&1 | grep -i usage || \
    log_fail "Usage message did not contain the word 'usage'."
zpool wait -t scrub fakepool 2>&1 | grep -i 'no such pool' || \
    log_fail "Error message did not contain phrase 'no such pool'."
zpool wait -t foo $TESTPOOL 2>&1 | grep -i 'invalid activity' || \
    log_fail "Error message did not contain phrase 'invalid activity'."

log_pass "'zpool wait' behaves sensibly when invoked incorrectly."
