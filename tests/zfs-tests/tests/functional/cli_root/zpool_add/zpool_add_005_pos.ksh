#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
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
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2012, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_add/zpool_add.kshlib

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
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
	poolexists $TESTPOOL1 && destroy_pool $TESTPOOL1

	if [[ -n $saved_dump_dev ]]; then
		log_must eval "dumpadm -u -d $saved_dump_dev > /dev/null"
	fi
}

log_assert "'zpool add' should fail with inapplicable scenarios."

log_onexit cleanup

mnttab_dev=$(find_mnttab_dev)
vfstab_dev=$(find_vfstab_dev)
saved_dump_dev=$(save_dump_dev)
dump_dev=$DISK2

create_pool $TESTPOOL $DISK0
log_must poolexists $TESTPOOL

create_pool $TESTPOOL1 $DISK1
log_must poolexists $TESTPOOL1

unset NOINUSE_CHECK
log_mustnot zpool add -f $TESTPOOL $DISK1
log_mustnot zpool add --allow-in-use $TESTPOOL $DISK1
log_mustnot zpool add -f $TESTPOOL $mnttab_dev
log_mustnot zpool add --allow-in-use $TESTPOOL $mnttab_dev
if is_linux; then
       log_mustnot zpool add $TESTPOOL $vfstab_dev
else
       log_mustnot zpool add -f $TESTPOOL $vfstab_dev
fi

if is_illumos; then
	log_must eval "new_fs ${DEV_DSKDIR}/$dump_dev > /dev/null 2>&1"
	log_must eval "dumpadm -u -d ${DEV_DSKDIR}/$dump_dev > /dev/null"
	log_mustnot zpool add -f $TESTPOOL $dump_dev
fi

log_pass "'zpool add' should fail with inapplicable scenarios."
