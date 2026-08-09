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
#	Destroyed pools devices was moved to another directory, it still can be
#	imported correctly.
#
# STRATEGY:
#	1. Create test pool A with several devices.
#	2. Destroy pool A.
#	3. Move devices to another directory.
#	4. Verify 'zpool import -D' succeed.
#

verify_runnable "global"

function cleanup
{
	destroy_pool $TESTPOOL1

	log_must rm -rf $DEVICE_DIR/*
	typeset i=0
	while (( i < $MAX_NUM )); do
		log_must mkfile $FILE_SIZE ${DEVICE_DIR}/${DEVICE_FILE}$i
		((i += 1))
	done
}

log_assert "Destroyed pools devices was moved to another directory," \
	"it still can be imported correctly."
log_onexit cleanup

log_must zpool create $TESTPOOL1 $VDEV0 $VDEV1 $VDEV2
typeset guid=$(get_config $TESTPOOL1 pool_guid)
typeset target=$TESTPOOL1
if (( RANDOM % 2 == 0 )) ; then
	target=$guid
	log_note "Import by guid."
fi
log_must zpool destroy $TESTPOOL1

log_note "Devices was moved to different directories."
log_must mkdir $DEVICE_DIR/newdir1 $DEVICE_DIR/newdir2
log_must mv $VDEV1 $DEVICE_DIR/newdir1
log_must mv $VDEV2 $DEVICE_DIR/newdir2
log_must zpool import -d $DEVICE_DIR/newdir1 -d $DEVICE_DIR/newdir2 \
	-d $DEVICE_DIR -D -f $target
log_must zpool destroy -f $TESTPOOL1

log_note "Devices was moved to same directory."
log_must mv $VDEV0 $DEVICE_DIR/newdir2
log_must mv $DEVICE_DIR/newdir1/* $DEVICE_DIR/newdir2
log_must zpool import -d $DEVICE_DIR/newdir2 -D -f $target
log_must zpool destroy -f $TESTPOOL1

log_pass "Destroyed pools devices was moved, 'zpool import -D' passed."
