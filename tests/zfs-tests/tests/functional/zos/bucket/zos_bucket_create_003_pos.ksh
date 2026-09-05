#!/bin/ksh -p

. $STF_SUITE/include/libtest.shlib

BUCKET1="multibucket_a"
BUCKET2="multibucket_b"
BUCKET3="multibucket_c"

log_assert "Multiple buckets can be created in the same pool"

log_must zfs bucket create $TESTPOOL $BUCKET1
log_must zfs bucket create $TESTPOOL $BUCKET2
log_must zfs bucket create $TESTPOOL $BUCKET3

log_must zfs bucket delete $TESTPOOL $BUCKET1
log_must zfs bucket delete $TESTPOOL $BUCKET2
log_must zfs bucket delete $TESTPOOL $BUCKET3

log_pass "Multiple bucket creation succeeded"
