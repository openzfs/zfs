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

#
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/rsend/rsend.kshlib

# DESCRIPTION:
#	zfs send -X without -R will fail.
#
# STRATEGY:
#	1. Setup test model
#	2. Run "zfs send -X random $POOL" and check for failure.

verify_runnable "both"

function cleanup
{
    cleanup_pool $POOL2
    cleanup_pool $POOL
    log_must setup_test_model $POOL
}

log_assert "zfs send -X without -R will fail"
log_onexit cleanup

cleanup

log_mustnot eval "zfs send -X $POOL/foobar $POOL@final"

log_pass "Ensure that zfs send -X without -R will fail"
