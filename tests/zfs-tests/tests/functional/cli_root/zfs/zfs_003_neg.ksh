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
# DESCRIPTION:
# zfs command will failed with unexpected scenarios:
# (1) ZFS_DEV cannot be opened
# (2) MNTTAB cannot be opened
#
# STRATEGY:
# 1. Create an array of zfs command
# 2. Execute each command in the array
# 3. Verify the command aborts and generate a core file
#

verify_runnable "global"

function cleanup
{
	for file in $ZFS_DEV $MNTTAB; do
		log_must eval "[ -e ${file} ] || mv ${file}.bak $file"
	done
}

log_assert "zfs fails with unexpected scenario."
log_onexit cleanup

#verify zfs failed if ZFS_DEV cannot be opened
ZFS_DEV=/dev/zfs

if is_linux; then
	# On Linux, we use /proc/self/mounts, which cannot be moved.
	MNTTAB=
fi

for file in $ZFS_DEV $MNTTAB; do
	log_must mv $file ${file}.bak
	for cmd in "" "list" "get all" "mount"; do
		log_mustnot eval "zfs $cmd >/dev/null 2>&1"
	done
	log_must mv ${file}.bak $file
done

log_pass "zfs fails with unexpected scenario as expected."
