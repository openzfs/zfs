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
# Copyright (c) 2018 by Nutanix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_import/zpool_import.cfg

#
# DESCRIPTION:
#	Make sure zpool import -d <device> works.
#
# STRATEGY:
#	1. Create test pool A.
#	2. Export pool A.
#	3. Verify 'import -d <device>' works
#

verify_runnable "global"

function cleanup
{
	destroy_pool $TESTPOOL1

	log_must rm $VDEV0 $VDEV1
	log_must truncate -s $FILE_SIZE $VDEV0 $VDEV1
}

log_assert "Pool can be imported with '-d <device>'"
log_onexit cleanup

log_must zpool create $TESTPOOL1 $VDEV0 $VDEV1
log_must zpool export $TESTPOOL1

log_must zpool import -d $VDEV0 -d $VDEV1 $TESTPOOL1
log_must zpool export $TESTPOOL1

# mix -d <dir> and -d <device>
log_must mkdir $DEVICE_DIR/test_dir
log_must ln -s $VDEV0 $DEVICE_DIR/test_dir/disk
log_must zpool import -d $DEVICE_DIR/test_dir -d $VDEV1 $TESTPOOL1

log_pass "Pool can be imported with '-d <device>'"
