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
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_add/zpool_add.kshlib
. $TMPFILE

#
# DESCRIPTION:
#       'zpool add' should return fail if
#	1. vdev is part of an active pool
#	2. vdev is currently mounted
#	3. vdev is in /etc/vfstab
#	3. vdev is specified as the dedicated dump device
#
# STRATEGY:
#	1. Create case scenarios
#	2. For each scenario, try to add the device to the pool
#	3. Verify the add operation get failed
#

verify_runnable "global"

function cleanup
{
	destroy_pool -f $TESTPOOL
	destroy_pool -f $TESTPOOL1

	if [[ -n $saved_dump_dev ]]; then
		log_must eval "$DUMPADM -u -d $saved_dump_dev > /dev/null"
	fi

	# Don't want to repartition the disk(s) on Linux.
	# We do that in setup.ksh in a very special way.
	[[ -z "$LINUX" ]] && partition_cleanup
}

log_assert "'zpool add' should fail with inapplicable scenarios."

log_onexit cleanup

typeset slice_part=s
[[ -n "$LINUX" ]] && slice_part=p

mnttab_dev=$(find_mnttab_dev)
vfstab_dev=$(find_vfstab_dev)
saved_dump_dev=$(save_dump_dev)
dump_dev=${disk}${slice_part}${SLICE3}

create_pool "$TESTPOOL" "${disk}${slice_part}${SLICE0}"
log_must poolexists "$TESTPOOL"

create_pool "$TESTPOOL1" "${disk}${slice_part}${SLICE1}"
log_must poolexists "$TESTPOOL1"
log_mustnot $ZPOOL add -f "$TESTPOOL" ${disk}${slice_part}${SLICE1}

log_mustnot $ZPOOL add -f "$TESTPOOL" $mnttab_dev

log_mustnot $ZPOOL add -f "$TESTPOOL" $vfstab_dev

log_must $ECHO "y" | $NEWFS $DEV_DSKDIR/$dump_dev > /dev/null 2>&1
log_must $DUMPADM -u -d $DEV_DSKDIR/$dump_dev > /dev/null
log_mustnot $ZPOOL add -f "$TESTPOOL" $dump_dev

log_pass "'zpool add' should fail with inapplicable scenarios."
