#!/bin/ksh -p
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
#

#
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2012 by Delphix. All rights reserved.
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

	log_must $RM -rf $DEVICE_DIR/*
	typeset i=0
	while (( i < $MAX_NUM )); do
		log_must $MKFILE $FILE_SIZE ${DEVICE_DIR}/${DEVICE_FILE}$i
		((i += 1))
	done
}

log_assert "Destroyed pools devices was moved to another directory," \
	"it still can be imported correctly."
log_onexit cleanup

log_must $ZPOOL create $TESTPOOL1 $VDEV0 $VDEV1 $VDEV2
typeset guid=$(get_config $TESTPOOL1 pool_guid)
typeset target=$TESTPOOL1
if (( RANDOM % 2 == 0 )) ; then
	target=$guid
	log_note "Import by guid."
fi
log_must $ZPOOL destroy $TESTPOOL1

log_note "Devices was moved to different directories."
log_must $MKDIR $DEVICE_DIR/newdir1 $DEVICE_DIR/newdir2
log_must $MV $VDEV1 $DEVICE_DIR/newdir1
log_must $MV $VDEV2 $DEVICE_DIR/newdir2
log_must $ZPOOL import -d $DEVICE_DIR/newdir1 -d $DEVICE_DIR/newdir2 \
	-d $DEVICE_DIR -D -f $target
log_must $ZPOOL destroy -f $TESTPOOL1

log_note "Devices was moved to same directory."
log_must $MV $VDEV0 $DEVICE_DIR/newdir2
log_must $MV $DEVICE_DIR/newdir1/* $DEVICE_DIR/newdir2
log_must $ZPOOL import -d $DEVICE_DIR/newdir2 -D -f $target
log_must $ZPOOL destroy -f $TESTPOOL1

log_pass "Destroyed pools devices was moved, 'zpool import -D' passed."
