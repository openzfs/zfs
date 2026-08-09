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
