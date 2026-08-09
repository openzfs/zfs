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
# Copyright 2014 Nexenta Systems, Inc.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#
# the zfs rootpool/rootfs can not be destroyed
#
# STRATEGY:
# 1) check if the current system is installed as zfs root
# 2) get the rootpool
# 3) try to destroy the rootpool, which should fail
# 4) try to destroy the rootpool filesystem, which should fail
#

verify_runnable "global"
log_assert "zpool/zfs destroy <rootpool> should fail"

typeset rootpool=$(get_rootpool)
typeset tmpfile="$TEST_BASE_DIR/mounted-datasets.$$"

# Collect the currently mounted ZFS filesystems, so that we can repair any
# damage done by the attempted pool destroy. The destroy itself should fail,
# but some filesystems can become unmounted in the process, and aren't
# automatically remounted.
mount -p | awk '$4 == "zfs" {print $1}' > $tmpfile

log_mustnot zpool destroy $rootpool

# Remount any filesystems that the destroy attempt unmounted.
while read ds; do
	mounted $ds || log_must zfs mount $ds
done < $tmpfile
rm -f $tmpfile

log_mustnot zfs destroy $rootpool

log_pass "rootpool/rootfs can not be destroyed"
