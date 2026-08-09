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

. $STF_SUITE/tests/functional/cli_root/zfs_promote/zfs_promote.cfg
. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	'zfs promote' can deal with conflicts in the namespaces.
#
# STRATEGY:
#	1. Create a snapshot and a clone of the snapshot
#	2. Create the same name snapshot for the clone
#	3. Promote the clone filesystem
#	4. Verify the promote operation fail due to the name conflicts.
#

verify_runnable "both"

function cleanup
{
	snapexists $snap && destroy_dataset $snap -rR

	typeset data
	for data in $TESTDIR/$TESTFILE0 $TESTDIR/$TESTFILE1; do
		[[ -e $data ]] && rm -f $data
	done
}

log_assert "'zfs promote' can deal with name conflicts."
log_onexit cleanup

snap=$TESTPOOL/$TESTFS@$TESTSNAP
clone=$TESTPOOL/$TESTCLONE
clonesnap=$TESTPOOL/$TESTCLONE@$TESTSNAP

# setup for promte testing
log_must mkfile $FILESIZE $TESTDIR/$TESTFILE0
log_must zfs snapshot $snap
log_must mkfile $FILESIZE $TESTDIR/$TESTFILE1
log_must rm -f $TESTDIR/$TESTFILE0
log_must zfs clone $snap $clone
log_must mkfile $FILESIZE /$clone/$CLONEFILE
log_must zfs snapshot $clonesnap

log_mustnot zfs promote $clone

log_pass "'zfs promote' deals with name conflicts as expected."

