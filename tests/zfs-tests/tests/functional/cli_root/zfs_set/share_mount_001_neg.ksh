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

. $STF_SUITE/tests/functional/cli_root/zfs_set/zfs_set_common.kshlib

#
# DESCRIPTION:
# Verify that we cannot share or mount legacy filesystems.
#
# STRATEGY:
# 1. Set mountpoint as legacy or none
# 2. Use zfs share or zfs mount to share or mount the filesystem
# 3. Verify that the command returns error
#

verify_runnable "both"

function cleanup
{
	log_must zfs set mountpoint=$oldmpt $fs
}

log_assert "Verify that we cannot share or mount legacy filesystems."
log_onexit cleanup

fs=$TESTPOOL/$TESTFS
oldmpt=$(get_prop mountpoint $fs)

for propval in "legacy" "none"; do
	log_must zfs set mountpoint=$propval $fs

	log_mustnot zfs mount $fs
	log_mustnot zfs share $fs
done

log_pass "We cannot share or mount legacy filesystems as expected."
