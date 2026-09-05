#!/bin/ksh -p

. $STF_SUITE/include/libtest.shlib

log_assert "Listing buckets on a pool with no buckets succeeds"

log_must zfs bucket list $TESTPOOL

log_pass "Empty bucket list succeeded"
