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
# Copyright (c) 2017 by Lawrence Livermore National Security, LLC.
#

# DESCRIPTION:
#	Verify import behavior for inactive, but not exported, pools
#
# STRATEGY:
#	1. Create a zpool
#	2. Verify multihost=off and hostids match (no activity check)
#	3. Verify multihost=off and hostids differ (no activity check)
#	4. Verify multihost=off and hostid allowed (no activity check)
#	5. Verify multihost=on and hostids match (activity check)
#	6. Verify multihost=on and hostids differ (activity check)
#	7. Verify mmp_write and mmp_fail are set correctly
#	8. Verify multihost=on and hostid zero fails (no activity check)
#	9. Verify activity check duration based on mmp_write and mmp_fail
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/mmp/mmp.cfg
. $STF_SUITE/tests/functional/mmp/mmp.kshlib

verify_runnable "both"

function cleanup
{
	mmp_pool_destroy $MMP_POOL $MMP_DIR
	log_must mmp_clear_hostid
	log_must set_tunable64 MULTIHOST_INTERVAL $MMP_INTERVAL_DEFAULT
}

log_assert "multihost=on|off inactive pool activity checks"
log_onexit cleanup

# 1. Create an enriched zpool (hole, indirect, log, cache, and spare
#    vdevs) so the activity checks exercise the non-writeable types.
mmp_pool_create_simple $MMP_POOL $MMP_DIR enriched

# 2. Verify multihost=off and hostids match (no activity check)
log_note "Verify multihost=off and hostids match (no activity check)"
log_must zpool set multihost=off $MMP_POOL

for opt in "" "-f"; do
	log_must zpool export -F $MMP_POOL
	log_must import_no_activity_check $MMP_POOL "-d $MMP_DIR $opt"
done

# 3. Verify multihost=off and hostids differ (no activity check)
log_note "Verify multihost=off and hostids differ (no activity check)"
log_must zpool export -F $MMP_POOL
log_must mmp_clear_hostid
log_must mmp_set_hostid $HOSTID2
log_mustnot import_no_activity_check $MMP_POOL "-d $MMP_DIR"
log_must import_no_activity_check $MMP_POOL "-d $MMP_DIR -f"

# 4. Verify multihost=off and hostid zero allowed (no activity check)
log_note "Verify multihost=off and hostid zero allowed (no activity check)"
log_must zpool export -F $MMP_POOL
log_must mmp_clear_hostid
log_mustnot import_no_activity_check $MMP_POOL "-d $MMP_DIR"
log_must import_no_activity_check $MMP_POOL "-d $MMP_DIR -f"

# 5. Verify multihost=on and hostids match (activity check)
log_note "Verify multihost=on and hostids match (activity check)"
log_must mmp_pool_set_hostid $MMP_POOL $HOSTID1
log_must zpool set multihost=on $MMP_POOL

for opt in "" "-f"; do
	log_must zpool export -F $MMP_POOL
	log_must import_activity_check $MMP_POOL "-d $MMP_DIR $opt"
done

# 6. Verify multihost=on and hostids differ (activity check)
log_note "Verify multihost=on and hostids differ (activity check)"
log_must zpool export -F $MMP_POOL
log_must mmp_clear_hostid
log_must mmp_set_hostid $HOSTID2
log_mustnot import_activity_check $MMP_POOL "-d $MMP_DIR"
log_must import_activity_check $MMP_POOL "-d $MMP_DIR -f"

# 7. Verify mmp_write and mmp_fail are set correctly
log_note "Verify mmp_write and mmp_fail are set correctly"
log_must zpool export -F $MMP_POOL
log_must verify_mmp_write_fail_present $MMP_DIR/vdev1

# 8. Verify multihost=on and hostid zero fails (no activity check)
log_note "Verify multihost=on and hostid zero fails (no activity check)"
log_must mmp_clear_hostid
MMP_IMPORTED_MSG="Set a unique system hostid"
log_must check_pool_import $MMP_POOL "-d $MMP_DIR -f" "action" "$MMP_IMPORTED_MSG"
log_mustnot import_no_activity_check $MMP_POOL "-d $MMP_DIR -f"

# 9. Verify activity check duration based on mmp_write and mmp_fail
# Specify a short test via tunables but import pool imported while
# tunables set to default duration.
log_note "Verify activity check duration based on mmp_write and mmp_fail"
log_must set_tunable64 MULTIHOST_INTERVAL $MMP_INTERVAL_MIN
log_must mmp_clear_hostid
log_must mmp_set_hostid $HOSTID1
log_must import_activity_check $MMP_POOL "-d $MMP_DIR -f"

log_pass "multihost=on|off inactive pool activity checks passed"
