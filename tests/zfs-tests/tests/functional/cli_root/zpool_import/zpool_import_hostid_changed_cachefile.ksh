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
# A pool that was cleanly exported should be importable from a cachefile
# without force even if the local hostid doesn't match the on-disk hostid.
#
# STRATEGY:
#	1. Set a hostid.
#	2. Create a pool with a cachefile.
#	3. Backup the cachfile.
#	4. Export the pool.
#	5. Change the hostid.
#	6. Verify that importing the pool from the cachefile succeeds
#	   without force.
#

verify_runnable "global"

function custom_cleanup
{
	rm -f $HOSTID_FILE $CPATH $CPATHBKP
	cleanup
}

log_onexit custom_cleanup

# 1. Set a hostid.
log_must zgenhostid -f $HOSTID1

# 2. Create a pool.
log_must zpool create -o cachefile=$CPATH $TESTPOOL1 $VDEV0

# 3. Backup the cachfile.
log_must cp $CPATH $CPATHBKP

# 4. Export the pool.
log_must zpool export $TESTPOOL1

# 5. Change the hostid.
log_must zgenhostid -f $HOSTID2

# 6. Verify that importing the pool from the cachefile succeeds without force.
log_must zpool import -c $CPATHBKP $TESTPOOL1

log_pass "zpool import can import cleanly exported pool from cachefile " \
  "when hostid changes."
