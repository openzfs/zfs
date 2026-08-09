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
