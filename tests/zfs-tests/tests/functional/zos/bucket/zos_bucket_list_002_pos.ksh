#!/bin/ksh -p

. $STF_SUITE/include/libtest.shlib

BUCKET="listbucket_one"

log_assert "Listing buckets shows a single bucket"

log_must zfs bucket create $TESTPOOL/$BUCKET
log_must eval "zfs bucket list $TESTPOOL | grep -q '$BUCKET'"

log_must zfs bucket delete $TESTPOOL/$BUCKET

log_pass "Single bucket listing succeeded"
