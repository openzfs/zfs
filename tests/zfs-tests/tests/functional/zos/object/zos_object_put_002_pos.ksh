#!/bin/ksh -p

. $STF_SUITE/include/libtest.shlib

BUCKET="put002"
KEY="testkey"

log_assert "Overwriting object from file succeeds"

# Create a temp file with known content
echo "hello zos" > /tmp/zos_test_data1
echo "hello zos, v2" > /tmp/zos_test_data2

log_must zfs bucket create $TESTPOOL/$BUCKET
log_must zfs object put $TESTPOOL/$BUCKET/$KEY /tmp/zos_test_data1
log_must zfs object put $TESTPOOL/$BUCKET/$KEY /tmp/zos_test_data2
log_must zfs object get $TESTPOOL/$BUCKET/$KEY /tmp/zos_test_data_out
log_must diff /tmp/zos_test_data2 /tmp/zos_test_data_out
log_must [ "$(wc -c < /tmp/zos_test_data2)" = "$(wc -c < /tmp/zos_test_data_out)" ]
log_must zfs bucket delete $TESTPOOL/$BUCKET

log_pass "Overwriting object from file succeeded"
