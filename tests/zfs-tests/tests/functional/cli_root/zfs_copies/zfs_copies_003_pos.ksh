#!/bin/ksh
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

. $STF_SUITE/tests/functional/cli_root/zfs_copies/zfs_copies.kshlib

#
# DESCRIPTION:
#	Verify that the volume space used by multiple copies is charged correctly
#
# STRATEGY:
#	1. Create volume;
#	2. Create ZFS filesystem based on the volume;
#	3. Set the copies property of volume to 1,2 or 3;
#	4. Copy specified size data into each filesystem;
#	5. Verify that the volume space is charged as expected.
#

verify_runnable "global"

function cleanup
{
	if poolexists $TESTPOOL1; then
		destroy_pool $TESTPOOL1
	fi

	datasetexists $vol && destroy_dataset $vol
}

log_assert "Verify that ZFS volume space used by multiple copies is charged correctly."
log_onexit cleanup
vol=$TESTPOOL/$TESTVOL1


for val in 1 2 3; do
	do_vol_test zfs $val
done

log_pass "The volume space used by multiple copies is charged correctly as expected. "
