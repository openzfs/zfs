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
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2012, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_create/zpool_create.shlib

#
# DESCRIPTION:
# 'zpool create' should return an error with VDEVs of size SPA_MINDEVSIZE
#
# STRATEGY:
# 1. Create an array of parameters
# 2. For each parameter in the array, execute 'zpool create'
# 3. Verify an error is returned.
#

log_assert "'zpool create' should return an error with VDEVs SPA_MINDEVSIZE"

verify_runnable "global"

function cleanup
{
	typeset pool

	for pool in $TOOSMALL $TESTPOOL1 $TESTPOOL; do
		poolexists $pool && destroy_pool $pool
	done

	rm -rf $TESTDIR
}
log_onexit cleanup

create_pool $TESTPOOL $DISK0
log_must zfs create $TESTPOOL/$TESTFS
log_must zfs set mountpoint=$TESTDIR $TESTPOOL/$TESTFS

typeset -l devsize=$(($SPA_MINDEVSIZE - 1024 * 1024))
for files in $TESTDIR/file1 $TESTDIR/file2 $TESTDIR/file3
do
	log_must truncate -s $devsize $files
done

set -A args \
	"$TOOSMALL $TESTDIR/file1" "$TESTPOOL1 $TESTDIR/file1 $TESTDIR/file2" \
        "$TOOSMALL mirror $TESTDIR/file1 $TESTDIR/file2" \
	"$TOOSMALL raidz $TESTDIR/file1 $TESTDIR/file2" \
	"$TOOSMALL draid $TESTDIR/file1 $TESTDIR/file2 $TESTDIR/file3"

typeset -i i=0
while [[ $i -lt ${#args[*]} ]]; do
	log_mustnot zpool create ${args[i]}
	((i = i + 1))
done

log_pass "'zpool create' with badly formed parameters failed as expected."
