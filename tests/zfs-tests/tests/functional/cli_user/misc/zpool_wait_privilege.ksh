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
# Copyright (c) 2019 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#
# zpool wait works when run as an unprivileged user
#

verify_runnable "global"

log_must zpool wait $TESTPOOL

# Make sure printing status works as unprivileged user.
output=$(zpool wait -H $TESTPOOL 1) || \
    log_fail "'zpool wait -H $TESTPOOL 1' failed"
# There should be one line of status output in a pool with no activity.
log_must eval '[[ $(wc -l <<<$output) -ge 1 ]]'

log_pass "zpool wait works when run as a user"
