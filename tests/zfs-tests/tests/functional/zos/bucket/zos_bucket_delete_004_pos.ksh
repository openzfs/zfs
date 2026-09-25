#!/bin/ksh -p

. $STF_SUITE/include/libtest.shlib

BUCKET="del_verify_bucket"

log_assert "Bucket is fully removed after deletion"

log_must zfs bucket create $TESTPOOL/$BUCKET
log_must zfs bucket delete $TESTPOOL/$BUCKET
log_must zfs bucket create $TESTPOOL/$BUCKET
log_must zfs bucket delete $TESTPOOL/$BUCKET

log_pass "Bucket fully removed after deletion"
