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
# Copyright (c) 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
#
# DESCRIPTION:
# 'zpool create' will fail with formal disk slice in swap
#
#
# STRATEGY:
# 1. Get all the disk devices in swap
# 2. For each device, try to create a new pool with this device
# 3. Verify the creation is failed.
#

verify_runnable "global"

function cleanup
{
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
}

if is_freebsd; then
	typeset swap_disks=$(swapinfo -l | awk '/\/dev/ {print $1}')
elif is_linux; then
	typeset swap_disks=$(swapon -s | awk '/\/dev/ {print $1}')
else
	typeset swap_disks=$(swap -l | awk '/c[0-9].*d[0-9].*s[0-9]/ {print $1}')
fi

log_assert "'zpool create' should fail with disk slice in swap."
log_onexit cleanup

for sdisk in $swap_disks; do
	for opt in "-n" "" "-f"; do
		log_mustnot zpool create $opt $TESTPOOL $sdisk
	done
done

log_pass "'zpool create' passed as expected with inapplicable scenario."
