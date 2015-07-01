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
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Verify that umount and destroy fail, and do not unshare the shared
# file system
#
# STRATEGY:
# 1. Share the filesystem via 'zfs set sharenfs'.
# 2. Try umount failure, and verify that the file system is still shared.
# 3. Try destroy failure, and verify that the file system is still shared.
#

verify_runnable "global"

function cleanup
{
	log_must cd $origdir

	log_must $ZFS set sharenfs=off $TESTPOOL/$TESTFS
	unshare_fs $TESTPOOL/$TESTFS

	destroy_dataset -f $TESTPOOL/$TESTFS@snapshot
	destroy_dataset -f $TESTPOOL/$TESTFS/fs2
}

log_assert "Verify that umount and destroy fail, and do not unshare the shared" \
	"file system"
log_onexit cleanup

typeset origdir=$PWD

# unmount fails will not unshare the shared filesystem
log_must $ZFS set sharenfs=on $TESTPOOL/$TESTFS
log_must is_shared $TESTDIR
if cd $TESTDIR ; then
	log_mustnot $ZFS umount $TESTPOOL/$TESTFS
else
	log_fail "cd $TESTDIR fails"
fi
log_must is_shared $TESTDIR

# destroy fails will not unshare the shared filesystem
log_must $ZFS create $TESTPOOL/$TESTFS/fs2
if cd $TESTDIR/fs2 ; then
	log_mustnot $ZFS destroy $TESTPOOL/$TESTFS/fs2
else
	log_fail "cd $TESTDIR/fs2 fails"
fi
log_must is_shared $TESTDIR/fs2

log_pass "Verify that umount and destroy fail, and do not unshare the shared" \
	"file system"
