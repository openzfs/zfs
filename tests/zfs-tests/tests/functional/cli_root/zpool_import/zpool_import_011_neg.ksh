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
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2012, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_import/zpool_import.cfg

#
# DESCRIPTION:
#	For strip pool, any destroyed pool devices was demaged, zpool import -D
#	will failed.
#
# STRATEGY:
#	1. Create strip pool A with three devices.
#	2. Destroy this pool B.
#	3. Create pool B with one of devices in step 1.
#	4. Verify 'import -D' pool A will failed whenever pool B was destroyed
#	   or not.
#

verify_runnable "global"

function cleanup
{
	destroy_pool $TESTPOOL1
	destroy_pool $TESTPOOL2

	#
	# Recreate virtual devices to avoid destroyed pool information on files.
	#
	log_must rm -rf $VDEV0 $VDEV1 $VDEV2
	log_must mkfile $FILE_SIZE $VDEV0 $VDEV1 $VDEV2
}

log_assert "For strip pool, any destroyed pool devices was demaged," \
	"zpool import -D will failed."
log_onexit cleanup

log_must zpool create $TESTPOOL1 $VDEV0 $VDEV1 $VDEV2
typeset guid=$(get_config $TESTPOOL1 pool_guid)
typeset target=$TESTPOOL1
if (( RANDOM % 2 == 0 )) ; then
	target=$guid
	log_note "Import by guid."
fi
log_must zpool destroy $TESTPOOL1
log_must zpool create $TESTPOOL2 $VDEV2

log_mustnot zpool import -d $DEVICE_DIR -D -f $target

log_must zpool destroy $TESTPOOL2
log_mustnot zpool import -d $DEVICE_DIR -D -f $target

log_pass "Any strip pool devices damaged, pool can't be import passed."
