#!/bin/ksh -p

. $STF_SUITE/include/libtest.shlib

BUCKET="nonexistent_bucket"

log_assert "Deleting a non-existent bucket fails"

log_mustnot zfs bucket delete $TESTPOOL $BUCKET

log_pass "Non-existent bucket deletion correctly rejected"
