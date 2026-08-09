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
# Copyright 2016, loli10K. All rights reserved.
# Copyright (c) 2018 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Verify that 'zfs destroy' on a shared dataset, will unshare it.
#
# STRATEGY:
# 1. Create and share a dataset with sharenfs.
# 2. Verify the dataset is shared.
# 3. Invoke 'zfs destroy' on the dataset.
# 4. Verify the dataset is not shared.
#

verify_runnable "global"

function cleanup
{
	datasetexists "$TESTPOOL/$TESTFS/shared1" && \
		destroy_dataset $TESTPOOL/$TESTFS/shared1 -f
}

log_assert "Verify 'zfs destroy' will unshare the dataset"
log_onexit cleanup

# 1. Create and share a dataset with sharenfs.
log_must zfs create \
	-o sharenfs=on -o mountpoint=$TESTDIR/1 $TESTPOOL/$TESTFS/shared1

#
# 2. Verify the datasets is shared.
#
log_must is_shared $TESTDIR/1

# 3. Invoke 'zfs destroy' on the dataset.
log_must zfs destroy -f $TESTPOOL/$TESTFS/shared1

# 4. Verify the dataset is not shared.
log_mustnot is_shared $TESTDIR/1

log_pass "'zfs destroy' will unshare the dataset."
