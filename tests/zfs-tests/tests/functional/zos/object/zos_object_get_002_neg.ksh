#!/bin/ksh -p

. $STF_SUITE/include/libtest.shlib

BUCKET="testbucket"
KEY="testkey"

log_assert "Get nonexistent object fails"

log_must zfs bucket create $TESTPOOL/$BUCKET
log_mustnot zfs object get $TESTPOOL/$BUCKET/$KEY
log_must zfs bucket delete $TESTPOOL/$BUCKET

log_pass "Get nonexistent object failed"
