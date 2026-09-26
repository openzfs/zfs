#!/bin/ksh -p

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
log_must zfs bucket delete $TESTPOOL/$BUCKET

log_pass "Get object to file succeeded"
