#! /bin/ksh -p
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
# Copyright (c) 2018 by Nutanix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Copy a large number of files between 2 directories
# within a zfs filesystem works without errors.
# This make sure zap upgrading and expanding works.
#
# STRATEGY:
#
# 1. Create NR_FILES files in directory src
# 2. Check the number of files is correct
# 3. Copy files from src to dst in readdir order
# 4. Check the number of files is correct
#

verify_runnable "global"

function cleanup
{
	rm -rf $TESTDIR/src $TESTDIR/dst
}

log_assert "Copy a large number of files between 2 directories" \
	"within a zfs filesystem works without errors"

log_onexit cleanup

NR_FILES=60000
BATCH=1000

log_must mkdir $TESTDIR/src $TESTDIR/dst

WD=$PWD
cd $TESTDIR/src
# create NR_FILES in BATCH at a time to prevent overflowing argument buffer
for i in $(seq $(($NR_FILES/$BATCH))); do touch $(seq $((($i-1)*$BATCH+1)) $(($i*$BATCH))); done
cd $WD

log_must test $NR_FILES -eq $(ls -U $TESTDIR/src | wc -l)

# copy files from src to dst, use cp_files to make sure we copy in readdir order
log_must cp_files $TESTDIR/src $TESTDIR/dst

log_must test $NR_FILES -eq $(ls -U $TESTDIR/dst | wc -l)

log_pass
