#!/bin/ksh -p

. $STF_SUITE/include/libtest.shlib

BUCKET="delbucket_001"

log_assert "Deleting an existing bucket succeeds"

log_must zfs bucket create $TESTPOOL $BUCKET
log_must zfs bucket delete $TESTPOOL $BUCKET

log_pass "Bucket deletion succeeded"
