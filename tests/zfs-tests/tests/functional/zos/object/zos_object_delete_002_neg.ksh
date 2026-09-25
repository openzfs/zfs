#!/bin/ksh -p

. $STF_SUITE/include/libtest.shlib

BUCKET="testbucket"

log_assert "Delete nonexistant object fails"

# Create a temp file with known content
echo "hello zos" > /tmp/zos_test_data

log_mustnot zfs object delete $TESTPOOL/putbucket/testkey

log_pass "Delete nonexistant object failed"
