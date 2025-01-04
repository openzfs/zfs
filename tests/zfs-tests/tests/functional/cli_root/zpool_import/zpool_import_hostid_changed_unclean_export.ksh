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
# http://www.illumos.org/license/CDDL.
#

#
# Copyright (c) 2021 by Delphix. All rights reserved.
# Copyright (c) 2023 by Klara, Inc.
#

. $STF_SUITE/tests/functional/cli_root/zpool_import/zpool_import.kshlib

#
# DESCRIPTION:
# A pool that wasn't cleanly exported should not be importable without force if
# the local hostid doesn't match the on-disk hostid.
#
# STRATEGY:
#	1. Set a hostid.
#	2. Create a pool.
#	3. Simulate the pool being torn down without export:
#	3.1. Sync then freeze the pool.
#	3.2. Export the pool (uncleanly).
#	3.3. Restore the device state from the copy.
#	4. Change the hostid.
#	5. Verify that importing the pool fails.
#	6. Verify that importing the pool with force succeeds.
#

verify_runnable "global"

function custom_cleanup
{
	rm -f $HOSTID_FILE $VDEV0.bak
	cleanup
}

log_onexit custom_cleanup

# 1. Set a hostid.
log_must zgenhostid -f $HOSTID1

# 2. Create a pool.
log_must zpool create $TESTPOOL1 $VDEV0

# 3. Simulate the pool being torn down without export.
sync_pool $TESTPOOL1
log_must zpool freeze $TESTPOOL1
log_must zpool export $TESTPOOL1

# 4. Change the hostid.
log_must zgenhostid -f $HOSTID2

# 5. Verify that importing the pool fails.
log_mustnot zpool import -d $DEVICE_DIR $TESTPOOL1

# 6. Verify that importing the pool with force succeeds.
log_must zpool import -d $DEVICE_DIR -f $TESTPOOL1

log_pass "zpool import requires force if not cleanly exported " \
    "and hostid changed."
