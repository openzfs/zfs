#!/usr/bin/ksh -p
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
# Copyright (c) 2013 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/cli_user/misc/misc.cfg
. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#
# zfs shows a usage message when run as a user
#
# STRATEGY:
# 1. Run zfs as a user
# 2. Verify it produces a usage message
#

function cleanup
{
	if [ -e /tmp/zfs_001_neg.$$.txt ]
	then
		$RM /tmp/zfs_001_neg.$$.txt
	fi
}

log_onexit cleanup
log_assert "zfs shows a usage message when run as a user"

eval "$ZFS > /tmp/zfs_001_neg.$$.txt 2>&1"
log_must $GREP "usage: zfs command args" /tmp/zfs_001_neg.$$.txt

log_pass "zfs shows a usage message when run as a user"
