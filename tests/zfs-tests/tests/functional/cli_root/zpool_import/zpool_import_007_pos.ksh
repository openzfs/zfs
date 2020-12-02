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
