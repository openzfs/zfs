#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# https://opensource.org/license/CDDL-1.0.
#

#
# Copyright (c) 2017, Lawrence Livermore National Security, LLC.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/include/math.shlib

#
# DESCRIPTION:
# Ensure stats presented in the dbufstats kstat are correct based on the
# dbufs kstat.
#
# STRATEGY:
# 1. Generate a file with random data in it
# 2. Store output from dbufs kstat
# 3. Store output from dbufstats kstat
# 4. Compare stats presented in dbufstats with stat generated using
#    dbufstat and the dbufs kstat output
#

DBUFSTATS_FILE=$(mktemp -t dbufstats.out.XXXXXX)
DBUFS_FILE=$(mktemp -t dbufs.out.XXXXXX)

function cleanup
{
	log_must rm -f $TESTDIR/file $DBUFS_FILE $DBUFSTATS_FILE
}

function testdbufstat # stat_name dbufstat_filter
{
        name=$1
        filter=""

        [[ -n "$2" ]] && filter="-F $2"

	from_dbufstat=$(grep "^$name " "$DBUFSTATS_FILE" | cut -f2 -d' ')
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
log_must eval "kstat -g dbufstats > $DBUFSTATS_FILE"

for level in {0..11}; do
	testdbufstat "cache_level_$level" "dbc=1,level=$level"
done

testdbufstat "cache_count" "dbc=1"
testdbufstat "hash_elements" ""

log_pass "dbufstats produces correct statistics passed"
