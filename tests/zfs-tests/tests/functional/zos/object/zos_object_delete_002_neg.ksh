#!/bin/ksh -p

. $STF_SUITE/include/libtest.shlib

BUCKET="testbucket"

log_assert "Delete nonexistent object fails"

# Create a temp file with known content
echo "hello zos" > /tmp/zos_test_data

log_mustnot zfs object delete $TESTPOOL/putbucket/testkey

log_pass "Delete nonexistent object failed"
