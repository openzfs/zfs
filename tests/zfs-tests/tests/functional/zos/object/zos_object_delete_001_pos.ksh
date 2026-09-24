#!/bin/ksh -p

. $STF_SUITE/include/libtest.shlib

BUCKET="testbucket"

log_assert "Put object from file succeeds"

# Create a temp file with known content
echo "hello zos" > /tmp/zos_test_data

log_must zfs bucket create $TESTPOOL putbucket
log_must zfs object put $TESTPOOL/putbucket/testkey /tmp/zos_test_data
log_must zfs object delete $TESTPOOL/putbucket/testkey
log_mustnot zfs object delete $TESTPOOL/putbucket/testkey
log_must zfs bucket delete $TESTPOOL putbucket

log_pass "Put object from file succeeded"
