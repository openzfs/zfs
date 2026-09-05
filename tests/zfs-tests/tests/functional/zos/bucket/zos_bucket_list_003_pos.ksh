#!/bin/ksh -p

. $STF_SUITE/include/libtest.shlib

BUCKET1="listbucket_multi_a"
BUCKET2="listbucket_multi_b"
BUCKET3="listbucket_multi_c"

log_assert "Listing buckets shows all buckets"

log_must zfs bucket create $TESTPOOL $BUCKET1
log_must zfs bucket create $TESTPOOL $BUCKET2
log_must zfs bucket create $TESTPOOL $BUCKET3

log_must eval "zfs bucket list $TESTPOOL | grep -q '$BUCKET1'"
log_must eval "zfs bucket list $TESTPOOL | grep -q '$BUCKET2'"
log_must eval "zfs bucket list $TESTPOOL | grep -q '$BUCKET3'"

log_must zfs bucket delete $TESTPOOL $BUCKET1
log_must zfs bucket delete $TESTPOOL $BUCKET2
log_must zfs bucket delete $TESTPOOL $BUCKET3

log_pass "Multiple bucket listing succeeded"
