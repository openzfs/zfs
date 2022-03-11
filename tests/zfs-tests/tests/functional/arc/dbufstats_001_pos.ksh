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
# Ensure stats presented in /proc/spl/kstat/zfs/dbufstats are correct
# based on /proc/spl/kstat/zfs/dbufs.
#
# STRATEGY:
# 1. Generate a file with random data in it
# 2. Store output from dbufs kstat
# 3. Store output from dbufstats kstat
# 4. Compare stats presented in dbufstats with stat generated using
#    dbufstat and the dbufs kstat output
#

DBUFSTATS_FILE=$(mktemp $TEST_BASE_DIR/dbufstats.out.XXXXXX)
DBUFS_FILE=$(mktemp $TEST_BASE_DIR/dbufs.out.XXXXXX)

function cleanup
{
	log_must rm -f $TESTDIR/file $DBUFS_FILE $DBUFSTATS_FILE
}

function testdbufstat # stat_name dbufstat_filter
{
        name=$1
        filter=""

        [[ -n "$2" ]] && filter="-F $2"

	if is_linux; then
		read -r _ _ from_dbufstat _ < <(grep -w "$name" "$DBUFSTATS_FILE")
	else
		from_dbufstat=$(awk "/dbufstats\.$name:/ { print \$2 }" \
		    "$DBUFSTATS_FILE")
	fi
	from_dbufs=$(dbufstat -bxn -i "$DBUFS_FILE" "$filter" | wc -l)

	within_tolerance $from_dbufstat $from_dbufs 15 \
	    || log_fail "Stat $name exceeded tolerance"
}

verify_runnable "both"

log_assert "dbufstats produces correct statistics"

log_onexit cleanup

log_must file_write -o create -f "$TESTDIR/file" -b 1048576 -c 20 -d R
sync_all_pools

log_must eval "kstat dbufs > $DBUFS_FILE"
log_must eval "kstat dbufstats '' > $DBUFSTATS_FILE"

for level in {0..11}; do
	testdbufstat "cache_level_$level" "dbc=1,level=$level"
done

testdbufstat "cache_count" "dbc=1"
testdbufstat "hash_elements" ""

log_pass "dbufstats produces correct statistics passed"
