#!/bin/ksh -p

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
# A pool that wasn't cleanly exported should not be importable from a cachefile
# without force if the local hostid doesn't match the on-disk hostid.
#
# STRATEGY:
#	1. Set a hostid.
#	2. Create a pool.
#	3. Backup the cachefile.
#	4. Simulate the pool being torn down without export:
#	4.1. Copy the underlying device state.
#	4.2. Export the pool.
#	4.3. Restore the device state from the copy.
#	5. Change the hostid.
#	6. Verify that importing the pool from the cachefile fails.
#	7. Verify that importing the pool from the cachefile with force
#	   succeeds.
#

verify_runnable "global"

function custom_cleanup
{
	rm -f $HOSTID_FILE $CPATH $CPATHBKP $VDEV0.bak
	cleanup
}

log_onexit custom_cleanup

# 1. Set a hostid.
log_must zgenhostid -f $HOSTID1

# 2. Create a pool.
log_must zpool create -o cachefile=$CPATH $TESTPOOL1 $VDEV0

# 3. Backup the cachfile.
log_must cp $CPATH $CPATHBKP

# 4. Simulate the pool being torn down without export.
log_must cp $VDEV0 $VDEV0.bak
log_must zpool export $TESTPOOL1
log_must cp -f $VDEV0.bak $VDEV0
log_must rm -f $VDEV0.bak

# 5. Change the hostid.
log_must zgenhostid -f $HOSTID2

# 6. Verify that importing the pool from the cachefile fails.
log_mustnot zpool import -c $CPATHBKP $TESTPOOL1

# 7. Verify that importing the pool from the cachefile with force succeeds.
log_must zpool import -f -c $CPATHBKP $TESTPOOL1

log_pass "zpool import from cachefile requires force if not cleanly " \
    "exported and hostid changes."
