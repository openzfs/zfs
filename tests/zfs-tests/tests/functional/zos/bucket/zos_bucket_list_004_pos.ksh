#!/bin/ksh -p

. $STF_SUITE/include/libtest.shlib

BUCKET="listbucket_after_del"

log_assert "Listing buckets after deletion does not show deleted bucket"

log_must zfs bucket create $TESTPOOL $BUCKET
log_must zfs bucket delete $TESTPOOL $BUCKET

OUTPUT=$(zfs bucket list $TESTPOOL)
[[ "$OUTPUT" != *"$BUCKET"* ]] || log_fail "Deleted bucket $BUCKET still in list"

log_pass "Deleted bucket correctly absent from list"
