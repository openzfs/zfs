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

#
# Copyright (c) 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Verify that zfs share should fail when sharing a shared zfs filesystem
#
# STRATEGY:
# 1. Make a zfs filesystem shared
# 2. Use zfs share to share the filesystem
# 3. Verify that zfs share returns error
#

verify_runnable "global"

function cleanup
{
	typeset val

	val=$(get_prop sharenfs $fs)
	if [[ $val == on ]]; then
		log_must zfs set sharenfs=off $fs
	fi
}

log_assert "zfs share fails with shared filesystem"
log_onexit cleanup

fs=$TESTPOOL/$TESTFS
sharenfs_val=$(get_prop sharenfs $fs)
mpt=$(get_prop mountpoint $fs)
if [[ $sharenfs_val == off ]]; then
	log_must zfs set sharenfs=on $fs
fi

if ! showshares_nfs | grep -q $mpt; then
	log_must zfs share $fs
fi

log_mustnot zfs share $fs

log_pass "zfs share fails with shared filesystem as expected."
