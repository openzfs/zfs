#!/bin/ksh -p
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or https://opensource.org/licenses/CDDL-1.0.
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
