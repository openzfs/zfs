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
