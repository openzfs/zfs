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
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/userquota/userquota_common.kshlib

#
# DESCRIPTION:
#       Check the user used size and defaultuserquota in zfs userspace
#
#
# STRATEGY:
#       1. set zfs defaultuserquota to a fs
#       2. write some data to the fs with specified user and size
#       3. use zfs userspace to check the used size and quota size
#

function cleanup
{
	datasetexists $snapfs && destroy_dataset $snapfs

	log_must cleanup_quota
}

log_onexit cleanup

log_assert "Check the zfs userspace used and defaultuserquota"

log_must zfs set defaultuserquota=100m $QFS

mkmount_writable $QFS

log_must user_run $QUSER1 mkfile 50m $QFILE
sync_all_pools

typeset snapfs=$QFS@snap

log_must zfs snapshot $snapfs

log_must eval "zfs userspace $QFS >/dev/null 2>&1"
log_must eval "zfs userspace $snapfs >/dev/null 2>&1"

for fs in "$QFS" "$snapfs"; do
	log_note "check the defaultuserquota size in zfs userspace $fs"
	log_must eval "zfs userspace $fs | grep $QUSER1 | grep 100M"

	log_note "check the user used size in zfs userspace $fs"
	log_must eval "zfs userspace $fs | grep $QUSER1 | grep 50\\.\*M"
done

log_pass "Check the zfs userspace used and defaultuserquota"
