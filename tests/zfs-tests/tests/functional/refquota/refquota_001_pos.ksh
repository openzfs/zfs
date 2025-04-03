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

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	refquota limits the amount of space a dataset can consume, but does
#	not include space used by descendents.
#
# STRATEGY:
#	1. Setting refquota in given filesystem
#	2. Create descendent filesystem
#	3. Verify refquota limits the amount of space a dataset can consume
#	4. Verify the limit does not impact descendents
#

verify_runnable "both"

function cleanup
{
	log_must zfs destroy -rf $TESTPOOL/$TESTFS
	log_must zfs create $TESTPOOL/$TESTFS
	log_must zfs set mountpoint=$TESTDIR $TESTPOOL/$TESTFS
}

log_assert "refquota limits the amount of space a dataset can consume, " \
	"but does not include space used by descendents."
log_onexit cleanup

fs=$TESTPOOL/$TESTFS
sub=$fs/sub
log_must zfs create $sub

log_must zfs set refquota=10M $fs
mntpnt=$(get_prop mountpoint $fs)

log_mustnot mkfile 11M $mntpnt/file
log_must mkfile 9M $mntpnt/file
log_must zfs snapshot $fs@snap
log_mustnot mkfile 2M $mntpnt/file2

mntpnt=$(get_prop mountpoint $sub)
log_must mkfile 10M $mntpnt/file
log_must zfs snapshot $sub@snap
log_must mkfile 10 $mntpnt/file2

log_pass "refquota limits the amount of space a dataset can consume, " \
	"but does not include space used by descendents."
