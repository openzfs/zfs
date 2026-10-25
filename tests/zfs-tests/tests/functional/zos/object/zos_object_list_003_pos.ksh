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

BUCKET="object_list_003"
KEY1="listbucket_multi_a"
KEY2="listbucket_multi_b"
KEY3="listbucket_multi_c"

log_assert "Listing objects shows all objects"

# Create a temp file with known content
echo "hello zos" > /tmp/zos_test_data1

log_must zfs bucket create $TESTPOOL/$BUCKET
log_must zfs object put $TESTPOOL/$BUCKET/$KEY1 /tmp/zos_test_data1
log_must zfs object put $TESTPOOL/$BUCKET/$KEY2 /tmp/zos_test_data1
log_must zfs object put $TESTPOOL/$BUCKET/$KEY3 /tmp/zos_test_data1

log_must eval "zfs object list $TESTPOOL/$BUCKET | grep -q '$KEY1'"
log_must eval "zfs object list $TESTPOOL/$BUCKET | grep -q '$KEY2'"
log_must eval "zfs object list $TESTPOOL/$BUCKET | grep -q '$KEY3'"

log_must zfs object delete $TESTPOOL/$BUCKET/$KEY1
log_must zfs object delete $TESTPOOL/$BUCKET/$KEY2
log_must zfs object delete $TESTPOOL/$BUCKET/$KEY3
log_must zfs bucket delete $TESTPOOL/$BUCKET

log_pass "Multiple object listing succeeded"
