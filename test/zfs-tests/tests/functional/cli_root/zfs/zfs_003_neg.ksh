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

log_assert "zfs fails with unexpected scenarios."

#verify zfs failed if ZFS_DEV cannot be opened
ZFS_DEV=/dev/zfs

for file in $ZFS_DEV $MNTTAB; do
	if [[ -e $file ]]; then
		$MV $file ${file}.bak
	fi
	for cmd in "" "list" "get all" "mount"; do
		log_mustnot eval "$ZFS $cmd >/dev/null 2>&1"
	done
	$MV ${file}.bak $file
done

log_pass "zfs fails with unexpected scenarios as expected."
