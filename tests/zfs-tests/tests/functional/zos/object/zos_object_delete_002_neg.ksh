#!/bin/ksh -p

. $STF_SUITE/include/libtest.shlib

BUCKET="del002"

log_assert "Delete nonexistent object fails"

# Create a temp file with known content
echo "hello zos" > /tmp/zos_test_data

log_mustnot zfs object delete $TESTPOOL/$BUCKET/testkey

log_pass "Delete nonexistent object failed"
