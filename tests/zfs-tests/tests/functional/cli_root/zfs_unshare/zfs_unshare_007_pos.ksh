#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or https://opensource.org/licenses/CDDL-1.0.
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
