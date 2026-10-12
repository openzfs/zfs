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

BUCKET="listbucket_after_del"

log_assert "Listing buckets after deletion does not show deleted bucket"

log_must zfs bucket create $TESTPOOL/$BUCKET
log_must zfs bucket delete $TESTPOOL/$BUCKET

OUTPUT=$(zfs bucket list $TESTPOOL)
[[ "$OUTPUT" != *"$BUCKET"* ]] || log_fail "Deleted bucket $BUCKET still in list"

log_pass "Deleted bucket correctly absent from list"
