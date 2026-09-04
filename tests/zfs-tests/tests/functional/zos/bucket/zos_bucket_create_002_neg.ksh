#!/bin/ksh -p

. $STF_SUITE/include/libtest.shlib

BUCKET="testbucket_dup"

log_assert "Creating a bucket succeeds"
log_must zfs bucket create $TESTPOOL $BUCKET
log_mustnot zfs bucket create $TESTPOOL $BUCKET
log_pass "Duplicate bucket creation correctly rejected"
