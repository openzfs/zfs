#!/bin/ksh -p

. $STF_SUITE/include/libtest.shlib

BUCKET="testbucket"

log_assert "Creating a bucket succeeds"
log_must zfs bucket create $TESTPOOL $BUCKET
log_pass "Bucket creation succeeded"
