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
#	Verify import behavior for exported pool (no activity check)
#
# STRATEGY:
#	1. Create a zpool
#	2. Verify multihost=off and hostids match (no activity check)
#	3. Verify multihost=off and hostids differ (no activity check)
#	4. Verify multihost=off and hostid zero allowed (no activity check)
#	5. Verify multihost=on and hostids match (no activity check)
#	6. Verify multihost=on and hostids differ (no activity check)
#	7. Verify multihost=on and hostid zero fails (no activity check)
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/mmp/mmp.cfg
. $STF_SUITE/tests/functional/mmp/mmp.kshlib

verify_runnable "both"

function cleanup
{
	default_cleanup_noexit
	log_must mmp_clear_hostid
}

log_assert "multihost=on|off activity checks exported pool"
log_onexit cleanup

# 1. Create a zpool
log_must mmp_set_hostid $HOSTID1
default_setup_noexit $DISK

# 2. Verify multihost=off and hostids match (no activity check)
log_must zpool set multihost=off $TESTPOOL

for opt in "" "-f"; do
	log_must zpool export $TESTPOOL
	log_must import_no_activity_check $TESTPOOL $opt
done

# 3. Verify multihost=off and hostids differ (no activity check)
for opt in "" "-f"; do
	log_must mmp_pool_set_hostid $TESTPOOL $HOSTID1
	log_must zpool export $TESTPOOL
	log_must mmp_clear_hostid
	log_must mmp_set_hostid $HOSTID2
	log_must import_no_activity_check $TESTPOOL $opt
done

# 4. Verify multihost=off and hostid zero allowed (no activity check)
log_must mmp_clear_hostid

for opt in "" "-f"; do
	log_must zpool export $TESTPOOL
	log_must import_no_activity_check $TESTPOOL $opt
done

# 5. Verify multihost=on and hostids match (no activity check)
log_must mmp_pool_set_hostid $TESTPOOL $HOSTID1
log_must zpool set multihost=on $TESTPOOL

for opt in "" "-f"; do
	log_must zpool export $TESTPOOL
	log_must import_no_activity_check $TESTPOOL $opt
done

# 6. Verify multihost=on and hostids differ (no activity check)
for opt in "" "-f"; do
	log_must mmp_pool_set_hostid $TESTPOOL $HOSTID1
	log_must zpool export $TESTPOOL
	log_must mmp_clear_hostid
	log_must mmp_set_hostid $HOSTID2
	log_must import_no_activity_check $TESTPOOL $opt
done

# 7. Verify multihost=on and hostid zero fails (no activity check)
log_must zpool export $TESTPOOL
log_must mmp_clear_hostid

for opt in "" "-f"; do
	MMP_IMPORTED_MSG="Set a unique system hostid"
	log_must check_pool_import $TESTPOOL "" "action" "$MMP_IMPORTED_MSG"
	log_mustnot import_no_activity_check $TESTPOOL $opt
done

log_pass "multihost=on|off exported pool activity checks passed"
