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
#	For raidz, one destroyed pools devices was removed or used by other
#	pool, it still can be imported correctly.
#
# STRATEGY:
#	1. Create a raidz pool A with N disks.
#	2. Destroy this pool A.
#	3. Create another pool B with 1 disk which was used by pool A.
#	4. Verify import this raidz pool can succeed.
#

verify_runnable "global"

function cleanup
{
	destroy_pool $TESTPOOL2
	destroy_pool $TESTPOOL1

	log_must rm -rf $DEVICE_DIR/*
	typeset i=0
	while (( i < $MAX_NUM )); do
		log_must mkfile $FILE_SIZE ${DEVICE_DIR}/${DEVICE_FILE}$i
		((i += 1))
	done
}

log_assert "For raidz, one destroyed pools devices was removed or used by " \
	"other pool, it still can be imported correctly."
log_onexit cleanup

log_must zpool create $TESTPOOL1 raidz $VDEV0 $VDEV1 $VDEV2 $VDEV3
typeset guid=$(get_config $TESTPOOL1 pool_guid)
typeset target=$TESTPOOL1
if (( RANDOM % 2 == 0 )) ; then
	target=$guid
	log_note "Import by guid."
fi
log_must zpool destroy $TESTPOOL1

log_must zpool create $TESTPOOL2 $VDEV0
log_must zpool import -d $DEVICE_DIR -D -f $target
log_must zpool destroy $TESTPOOL1

log_must zpool destroy $TESTPOOL2
log_must rm -rf $VDEV0
log_must zpool import -d $DEVICE_DIR -D -f $target
log_must zpool destroy $TESTPOOL1

log_note "For raidz, two destroyed pool's devices were used, import failed."
log_must mkfile $FILE_SIZE $VDEV0
log_must zpool create $TESTPOOL2 $VDEV0 $VDEV1
log_mustnot zpool import -d $DEVICE_DIR -D -f $target
log_must zpool destroy $TESTPOOL2

log_pass "zpool import -D raidz passed."
