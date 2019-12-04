#!/bin/ksh -p
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
# Copyright (c) 2017 by Lawrence Livermore National Security, LLC.
#

# DESCRIPTION:
#	Under no circumstances when multihost is active, should an active pool
#	with one hostid be importable by a host with a different hostid.
#
# STRATEGY:
#	 1. Simulate an active pool on another host with ztest.
#	 2. Verify 'zpool import' reports an active pool.
#	 3. Verify 'zpool import [-f] $MMP_POOL' cannot import the pool.
#	 4. Kill ztest to make pool eligible for import.
#	 5. Verify 'zpool import' fails with the expected error message.
#	 6. Verify 'zpool import $MMP_POOL' fails with the expected message.
#	 7. Verify 'zpool import -f $MMP_POOL' can now import the pool.
#	 8. Verify pool may be exported/imported without -f argument.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/mmp/mmp.cfg
. $STF_SUITE/tests/functional/mmp/mmp.kshlib

verify_runnable "both"

function cleanup
{
	mmp_pool_destroy $MMP_POOL $MMP_DIR
	log_must mmp_clear_hostid
	ZTESTPID=$(pgrep ztest)
	if [ -n "$ZTESTPID" ]; then
		for pid in $ZTESTPID; do
			log_must kill -9 $pid
		done
	else
		# if ztest not running and log present, ztest crashed
		if [ -f $MMP_ZTEST_LOG ]; then
			log_note "ztest appears to have crashed.  Tail of log:"
			tail -n 50 $MMP_ZTEST_LOG
		fi
	fi
}

log_assert "multihost=on|off active pool activity checks"
log_onexit cleanup

# 1. Simulate an active pool on another host with ztest.
mmp_pool_destroy $MMP_POOL $MMP_DIR
mmp_pool_create $MMP_POOL $MMP_DIR

# 2. Verify 'zpool import' reports an active pool.
log_must mmp_set_hostid $HOSTID2
log_must is_pool_imported $MMP_POOL "-d $MMP_DIR"

# 3. Verify 'zpool import [-f] $MMP_POOL' cannot import the pool.
MMP_IMPORTED_MSG="Cannot import '$MMP_POOL': pool is imported"

log_must try_pool_import $MMP_POOL "-d $MMP_DIR" "$MMP_IMPORTED_MSG"
for i in {1..10}; do
	log_must try_pool_import $MMP_POOL "-f -d $MMP_DIR" "$MMP_IMPORTED_MSG"
done

log_must try_pool_import $MMP_POOL "-c ${MMP_CACHE}.stale" "$MMP_IMPORTED_MSG"

for i in {1..10}; do
	log_must try_pool_import $MMP_POOL "-f -c ${MMP_CACHE}.stale" \
	    "$MMP_IMPORTED_MSG"
done

# 4. Kill ztest to make pool eligible for import.  Poll with 'zpool status'.
ZTESTPID=$(pgrep ztest)
if [ -n "$ZTESTPID" ]; then
	log_must kill -9 $ZTESTPID
fi
log_must wait_pool_imported $MMP_POOL "-d $MMP_DIR"
if [ -f $MMP_ZTEST_LOG ]; then
	log_must rm $MMP_ZTEST_LOG
fi

# 5. Verify 'zpool import' fails with the expected error message, when
#    - hostid=0:        - configuration error
#    - hostid=matches   - safe to import the pool
#    - hostid=different - previously imported on a different system
#
log_must mmp_clear_hostid
MMP_IMPORTED_MSG="Set a unique system hostid"
log_must check_pool_import $MMP_POOL "-d $MMP_DIR" "action" "$MMP_IMPORTED_MSG"

log_must mmp_set_hostid $HOSTID1
MMP_IMPORTED_MSG="The pool can be imported"
log_must check_pool_import $MMP_POOL "-d $MMP_DIR" "action" "$MMP_IMPORTED_MSG"

log_must mmp_clear_hostid
log_must mmp_set_hostid $HOSTID2
MMP_IMPORTED_MSG="The pool was last accessed by another system."
log_must check_pool_import $MMP_POOL "-d $MMP_DIR" "status" "$MMP_IMPORTED_MSG"

# 6. Verify 'zpool import $MMP_POOL' fails with the expected message.
MMP_IMPORTED_MSG="pool was previously in use from another system."
log_must try_pool_import $MMP_POOL "-d $MMP_DIR" "$MMP_IMPORTED_MSG"

# 7. Verify 'zpool import -f $MMP_POOL' can now import the pool.
log_must import_activity_check $MMP_POOL "-f -d $MMP_DIR"

# 8 Verify pool may be exported/imported without -f argument.
log_must zpool export $MMP_POOL
log_must import_no_activity_check $MMP_POOL "-d $MMP_DIR"

log_pass "multihost=on|off active pool activity checks passed"
