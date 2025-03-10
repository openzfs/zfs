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
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/rsend/rsend.kshlib

#
# DESCRIPTION:
#	zfs receive can handle out of space correctly.
#
# STRATEGY:
#	1. Create two pools, one is big and another is small.
#	2. Fill the big pool with data.
#	3. Take snapshot and backup the whole pool.
#	4. Receive this stream in small pool.
#	5. Verify zfs receive can handle the out of space error correctly.
#

verify_runnable "global"

BPOOL=bpool_test
SPOOL=spool_test

function cleanup
{
	if datasetexists $BPOOL ; then
		log_must_busy zpool destroy -f $BPOOL
	fi
	if datasetexists $SPOOL ; then
		log_must_busy zpool destroy -f $SPOOL
	fi
}

log_assert "Verify zfs receive can handle out of space correctly."
log_onexit cleanup

log_must mkfile $MINVDEVSIZE $TESTDIR/bfile
log_must mkfile $SPA_MINDEVSIZE  $TESTDIR/sfile
log_must zpool create -O compression=off $BPOOL $TESTDIR/bfile
log_must zpool create -O compression=off $SPOOL $TESTDIR/sfile

#
# Test out of space on sub-filesystem
#
log_must zfs create $BPOOL/fs
log_must mkfile 30M /$BPOOL/fs/file

log_must zfs snapshot $BPOOL/fs@snap
log_must eval "zfs send -R $BPOOL/fs@snap > $BACKDIR/fs-R"
log_mustnot eval "zfs receive -d -F $SPOOL < $BACKDIR/fs-R"

log_must datasetnonexists $SPOOL/fs
log_must ismounted $SPOOL

#
# Test out of space on top filesystem
#
log_must mv /$BPOOL/fs/file /$BPOOL
log_must_busy zfs destroy -rf $BPOOL/fs

log_must zfs snapshot $BPOOL@snap
log_must eval "zfs send -R $BPOOL@snap > $BACKDIR/bpool-R"
log_mustnot eval "zfs receive -d -F $SPOOL < $BACKDIR/bpool-R"

log_must datasetnonexists $SPOOL/fs
log_must ismounted $SPOOL

log_pass "zfs receive can handle out of space correctly."
