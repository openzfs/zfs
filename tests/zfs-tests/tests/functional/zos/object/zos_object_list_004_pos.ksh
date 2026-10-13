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

BUCKET="object_list_004"
KEY="testkey"

log_assert "Listing objects after deletion does not show deleted objects"

# Create a temp file with known content
echo "hello zos" > /tmp/zos_test_data1

log_must zfs bucket create $TESTPOOL/$BUCKET
log_must zfs object put $TESTPOOL/$BUCKET/$KEY /tmp/zos_test_data1
log_must zfs object delete $TESTPOOL/$BUCKET/$KEY

OUTPUT=$(zfs object list $TESTPOOL/$BUCKET)
[[ "$OUTPUT" != *"$KEY"* ]] || log_fail "Deleted object $KEY still in list"

log_must zfs bucket delete $TESTPOOL/$BUCKET

log_pass "Deleted object correctly absent from list"
