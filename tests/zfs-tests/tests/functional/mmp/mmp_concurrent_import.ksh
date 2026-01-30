#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# CDDL HEADER START
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# http://www.illumos.org/license/CDDL.
#
# CDDL HEADER END
#

#
# Copyright (c) 2026 by Lawrence Livermore National Security, LLC.
#

# DESCRIPTION:
#	Verify that even when importing a shared pool simultaneously
#	on systems with different host ids at most one will succeed.
#
# STRATEGY:
# 1. Create an multihost enabled pool
# 2. zhack imports: $HOSTID1 (matching) and $HOSTID1 (matching)
# 3. zhack imports: $HOSTID1 (matching) and $HOSTID2 (different)
# 4. zhack imports: $HOSTID3 (different) and $HOSTID4 (different)
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/mmp/mmp.cfg
. $STF_SUITE/tests/functional/mmp/mmp.kshlib

verify_runnable "both"

function cleanup
{
	ZHACKPIDS=$(pgrep zhack)
	if [ -n "$ZHACKPIDS" ]; then
		for pid in $ZHACKPIDS; do
			log_must kill -9 $pid
		done
	fi

	log_must rm -f $MMP_ZHACK_LOG.1 $MMP_ZHACK_LOG.2

	mmp_pool_destroy $MMP_POOL $MMP_DIR
	log_must mmp_clear_hostid
}

# Verify that pool was imported by at most one of the zhack processes.
# Check both the return code and expected import message.
function verify_zhack
{
	IMPORT_COUNT=0
	IMPORT_MSGS=0

	ZHACKPIDS=$(pgrep zhack)
	for pid in $ZHACKPIDS; do
		wait $pid
		STATUS=$?
		if [[ $STATUS -eq 0 ]]; then
			(( IMPORT_COUNT++ ))
		fi
		log_note "PID $pid exited with status $STATUS"
	done

	grep -H "Imported pool $MMP_POOL" $MMP_ZHACK_LOG.1 && (( IMPORT_MSGS++ ))
	grep -H "Imported pool $MMP_POOL" $MMP_ZHACK_LOG.2 && (( IMPORT_MSGS++ ))

	if [[ $IMPORT_MSGS -gt 1 ]]; then
		cat $MMP_ZHACK_LOG.*
		log_fail "Multiple import success messages"
	fi

	if [[ $IMPORT_COUNT -gt 1 ]]; then
		cat $MMP_ZHACK_LOG.*
		log_fail "Multiple import success return codes"
	fi

	if [[ $IMPORT_MSGS -ne $IMPORT_COUNT ]]; then
		cat $MMP_ZHACK_LOG.*
		log_fail "Messages ($IMPORT_MSGS) differs from count ($IMPORT_COUNT)"
	fi
}

OPTS="-d $MMP_DIR action idle -t5 $MMP_POOL"

log_assert "multihost=on concurrent imports"
log_onexit cleanup

# 1. Create a multihost enabled pool with HOSTID1
mmp_pool_create_simple $MMP_POOL $MMP_DIR
log_must zpool export -F $MMP_POOL

# 2. zhack imports: $HOSTID1 (matching) and $HOSTID1 (matching)
# Activity check required because the pool was exported with -F above, the
# claim phase will detect the double import despite matching hostids.
log_note "zhack import with $HOSTID1 (matching) and $HOSTID1 (matching)"
log_must eval "ZFS_HOSTID=$HOSTID1 zhack $OPTS >$MMP_ZHACK_LOG.1 2>&1 &"
log_must eval "ZFS_HOSTID=$HOSTID1 zhack $OPTS >$MMP_ZHACK_LOG.2 2>&1 &"
log_must verify_zhack

mmp_clear_hostid
mmp_set_hostid $HOSTID1
log_must import_activity_check $MMP_POOL "-d $MMP_DIR"
log_must zpool export $MMP_POOL

# 3. zhack imports: $HOSTID1 (matching) and $HOSTID2 (different)
# Activity check skipped for HOSTID1 it is expected to import successfully.
# zhack with HOSTID2 will run the activity check and detect the active pool.
log_note "zhack import with $HOSTID1 (matching) and $HOSTID2 (different)"
log_must eval "ZFS_HOSTID=$HOSTID1 zhack $OPTS >$MMP_ZHACK_LOG.1 2>&1 &"
log_must eval "ZFS_HOSTID=$HOSTID2 zhack $OPTS >$MMP_ZHACK_LOG.2 2>&1 &"
log_must verify_zhack

mmp_clear_hostid
mmp_set_hostid $HOSTID3
log_must import_activity_check $MMP_POOL "-d $MMP_DIR"
log_must zpool export $MMP_POOL

# 4. zhack imports: $HOSTID1 (different) and $HOSTID2 (different)
# Both zhacks will run the activity checks, depending on the exact timing
# one may succeed and the other fail, or both may fail.
log_note "zhack import with $HOSTID1 (different) and $HOSTID2 (different)"
log_must eval "ZFS_HOSTID=$HOSTID1 zhack $OPTS >$MMP_ZHACK_LOG.1 2>&1 &"
log_must eval "ZFS_HOSTID=$HOSTID2 zhack $OPTS >$MMP_ZHACK_LOG.2 2>&1 &"
log_must verify_zhack

log_pass "multihost=on concurrent imports"
