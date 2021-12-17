#!/bin/ksh

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
# Copyright (c) 2014, 2018 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/rsend/rsend.kshlib

#
# Description:
# Verify that error conditions don't cause panics in zfs send
#
# Strategy:
# 1. Perform a zfs incremental send from a bookmark that doesn't exist
# 2. Perform a zfs incremental replication send with incremental source
#    same as target (#11121)
#

verify_runnable "both"

log_mustnot eval "zfs send -i \#bla $POOl/$FS@final > /dev/null"

log_must eval "zfs send -R -i snapA $POOL/vol@snapA 2>&1 " \
    "> /dev/null | grep -q WARNING"

log_pass "Ensure that error conditions cause appropriate failures."
