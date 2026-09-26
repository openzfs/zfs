#!/bin/ksh -p

. $STF_SUITE/include/libtest.shlib

BUCKET="del001"
KEY="key"

log_assert "Deleting object succeeds"

# Create a temp file with known content
echo "hello zos" > /tmp/zos_test_data

log_must zfs bucket create $TESTPOOL/$BUCKET
log_must zfs object put $TESTPOOL/$BUCKET/$KEY /tmp/zos_test_data
log_must zfs object delete $TESTPOOL/$BUCKET/$KEY
log_mustnot zfs object delete $TESTPOOL/$BUCKET/$KEY
log_must zfs bucket delete $TESTPOOL/$BUCKET

log_pass "Deleting object succeeded"
