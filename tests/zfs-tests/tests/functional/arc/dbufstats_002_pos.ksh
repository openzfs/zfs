#!/bin/ksh -p
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
#

#
# Copyright (c) 2017, Lawrence Livermore National Security, LLC.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/include/math.shlib

#
# DESCRIPTION:
# Ensure that dbufs move from mru to mfu as expected.
#
# STRATEGY:
# 1. Set dbuf cache size to a small size (10M for this test)
# 2. Generate a file with random data (small enough to fit in cache)
# 3. zpool sync to remove dbufs from anon list in ARC
# 4. Obtain the object ID using linux stat command
# 5. Ensure that all dbufs are on the mru list in the ARC
# 6. Generate another random file large enough to flush dbuf cache
# 7. cat the first generated file
# 8. Ensure that at least some dbufs moved to the mfu list in the ARC
#

DBUFS_FILE=$(mktemp $TEST_BASE_DIR/dbufs.out.XXXXXX)

function cleanup
{
	log_must rm -f $TESTDIR/file $TESTDIR/file2 $DBUFS_FILE
}

verify_runnable "both"

log_assert "dbufs move from mru to mfu list"

log_onexit cleanup

log_must file_write -o create -f "$TESTDIR/file" -b 1048576 -c 1 -d R
sync_all_pools

objid=$(get_objnum "$TESTDIR/file")
log_note "Object ID for $TESTDIR/file is $objid"

log_must eval "kstat dbufs > $DBUFS_FILE"
dbuf=$(dbufstat -bxn -i "$DBUFS_FILE" -F "object=$objid" | wc -l)
mru=$(dbufstat -bxn -i "$DBUFS_FILE" -F "object=$objid,list=1" | wc -l)
mfu=$(dbufstat -bxn -i "$DBUFS_FILE" -F "object=$objid,list=3" | wc -l)
log_note "dbuf count is $dbuf, mru count is $mru, mfu count is $mfu"
verify_ne "0" "$mru" "mru count"
verify_eq "0" "$mfu" "mfu count"

log_must eval "cat $TESTDIR/file > /dev/null"
log_must eval "kstat dbufs > $DBUFS_FILE"
dbuf=$(dbufstat -bxn -i "$DBUFS_FILE" -F "object=$objid" | wc -l)
mru=$(dbufstat -bxn -i "$DBUFS_FILE" -F "object=$objid,list=1" | wc -l)
mfu=$(dbufstat -bxn -i "$DBUFS_FILE" -F "object=$objid,list=3" | wc -l)
log_note "dbuf count is $dbuf, mru count is $mru, mfu count is $mfu"
verify_ne "0" "$mfu" "mfu count"

log_pass "dbufs move from mru to mfu list passed"
