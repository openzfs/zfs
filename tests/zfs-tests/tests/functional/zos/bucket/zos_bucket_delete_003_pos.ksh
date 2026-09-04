#!/bin/ksh -p

. $STF_SUITE/include/libtest.shlib

BUCKET="delbucket_003"

log_assert "Bucket can be recreated after deletion"

log_must zfs bucket create $TESTPOOL $BUCKET
log_must zfs bucket delete $TESTPOOL $BUCKET
log_must zfs bucket create $TESTPOOL $BUCKET
log_must zfs bucket delete $TESTPOOL $BUCKET

log_pass "Bucket recreation after deletion succeeded"
