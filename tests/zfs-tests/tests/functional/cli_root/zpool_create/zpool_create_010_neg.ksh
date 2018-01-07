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
        poolexists $TOOSMALL && destroy_pool $TOOSMALL
        poolexists $TESTPOOL1 && destroy_pool $TESTPOOL1

        poolexists $TESTPOOL && destroy_pool $TESTPOOL

	[[ -d $TESTDIR ]] && rm -rf $TESTDIR

	partition_disk $SIZE $disk 6
}
log_onexit cleanup

if [[ -n $DISK ]]; then
        disk=$DISK
else
        disk=$DISK0
fi

create_pool $TESTPOOL $disk
log_must zfs create $TESTPOOL/$TESTFS
log_must zfs set mountpoint=$TESTDIR $TESTPOOL/$TESTFS

typeset -l devsize=$(($SPA_MINDEVSIZE - 1024 * 1024))
for files in $TESTDIR/file1 $TESTDIR/file2
do
	log_must mkfile $devsize $files
done

set -A args \
	"$TOOSMALL $TESTDIR/file1" "$TESTPOOL1 $TESTDIR/file1 $TESTDIR/file2" \
        "$TOOSMALL mirror $TESTDIR/file1 $TESTDIR/file2" \
	"$TOOSMALL raidz $TESTDIR/file1 $TESTDIR/file2"

typeset -i i=0
while [[ $i -lt ${#args[*]} ]]; do
	log_mustnot zpool create ${args[i]}
	((i = i + 1))
done

log_pass "'zpool create' with badly formed parameters failed as expected."
