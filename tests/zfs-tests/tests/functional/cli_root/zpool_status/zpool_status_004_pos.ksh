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
# Copyright (c) 2019 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Verify feature@headerrlog=disabled works perfectly.
#
# STRATEGY:
# 1. Create a file system
# 2. zinject checksum errors
# 3. Read the file
# 4. Take a snapshot and make a clone
# 5. Verify we see "snapshot, clone and filesystem" output in 'zpool status -v'

function cleanup
{
	log_must zfs destroy -R $TESTPOOL/$TESTFS@snap
	log_must zinject -c all
}

verify_runnable "both"

log_assert "Verify correct 'zpool status -v' output with a corrupted file"
log_onexit cleanup

log_must mkfile 10m $TESTDIR/10m_file

log_must zpool export $TESTPOOL
log_must zpool import $TESTPOOL

log_must zinject -t data -e checksum -f 100 $TESTDIR/10m_file

# Try to read the 2nd megabyte of 10m_file
dd if=$TESTDIR/10m_file bs=1M skip=1 count=1 || true

log_must zfs snapshot $TESTPOOL/$TESTFS@snap
log_must zfs clone $TESTPOOL/$TESTFS@snap $TESTPOOL/testclone

# Look to see that snapshot, clone and filesystem our files report errors
log_must eval "zpool status -v | grep '$TESTPOOL/$TESTFS@snap:/10m_file'"
log_must eval "zpool status -v | grep '$TESTPOOL/testclone/10m_file'"
log_must eval "zpool status -v | grep '$TESTDIR/10m_file'"

log_pass "'feature@headerrlog=disabled works perfectly"
