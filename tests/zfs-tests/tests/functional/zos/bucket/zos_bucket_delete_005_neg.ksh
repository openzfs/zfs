#!/bin/ksh -p

. $STF_SUITE/include/libtest.shlib

BUCKET="delete005"
KEY="key"

log_assert "Bucket will not be deleted if object exists"

echo "hello zos" > /tmp/zos_test_data_delete_005

log_must zfs bucket create $TESTPOOL/$BUCKET
log_must zfs object put $TESTPOOL/$BUCKET/$KEY /tmp/zos_test_data_delete_005
log_mustnot zfs bucket delete $TESTPOOL/$BUCKET
log_must zfs object delete $TESTPOOL/$BUCKET/$KEY
log_must zfs bucket delete $TESTPOOL/$BUCKET

log_pass "Bucket removed after object deletion"
