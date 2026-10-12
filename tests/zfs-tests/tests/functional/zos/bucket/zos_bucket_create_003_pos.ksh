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

. $STF_SUITE/include/libtest.shlib

BUCKET1="multibucket_a"
BUCKET2="multibucket_b"
BUCKET3="multibucket_c"

log_assert "Multiple buckets can be created in the same pool"

log_must zfs bucket create $TESTPOOL/$BUCKET1
log_must zfs bucket create $TESTPOOL/$BUCKET2
log_must zfs bucket create $TESTPOOL/$BUCKET3

log_must zfs bucket delete $TESTPOOL/$BUCKET1
log_must zfs bucket delete $TESTPOOL/$BUCKET2
log_must zfs bucket delete $TESTPOOL/$BUCKET3

log_pass "Multiple bucket creation succeeded"
