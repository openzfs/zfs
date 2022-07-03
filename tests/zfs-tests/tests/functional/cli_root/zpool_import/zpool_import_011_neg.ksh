#!/bin/ksh -p
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or https://opensource.org/licenses/CDDL-1.0.
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
