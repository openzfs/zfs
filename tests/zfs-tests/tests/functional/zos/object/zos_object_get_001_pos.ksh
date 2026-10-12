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

BUCKET="get001"
KEY="testkey"

log_assert "Get object to file succeeds"

# Create a temp file with known content
echo "hello zos" > /tmp/zos_test_data1

log_must zfs bucket create $TESTPOOL/$BUCKET
log_must zfs object put $TESTPOOL/$BUCKET/$KEY /tmp/zos_test_data1
log_must zfs object get $TESTPOOL/$BUCKET/$KEY /tmp/zos_test_data2
log_must diff /tmp/zos_test_data1 /tmp/zos_test_data2
log_must zfs object delete $TESTPOOL/$BUCKET/$KEY
log_must zfs bucket delete $TESTPOOL/$BUCKET

log_pass "Get object to file succeeded"
