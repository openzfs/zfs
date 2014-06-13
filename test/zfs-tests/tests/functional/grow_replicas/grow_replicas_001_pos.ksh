#! /bin/ksh -p
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

#
# Copyright (c) 2013 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/grow_replicas/grow_replicas.cfg
. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# A ZFS file system is limited by the amount of disk space
# available to the pool. Growing the pool by adding a disk
# increases the amount of space.
#
# STRATEGY:
# 1) Fill a ZFS filesystem mirror/raidz until ENOSPC by creating lots
# of files
# 2) Grow the mirror/raidz by adding a disk
# 3) Verify that more data can now be written to the file system
#

verify_runnable "global"

log_assert "A zpool mirror/raidz may be increased in capacity by adding a disk."

log_must $ZFS set compression=off $TESTPOOL/$TESTFS
$FILE_WRITE -o create -f $TESTDIR/$TESTFILE1 \
        -b $BLOCK_SIZE -c $WRITE_COUNT -d 0

typeset -i zret=$?
readonly ENOSPC=28
if [[ $zret -ne $ENOSPC ]]; then
        log_fail "file_write completed w/o ENOSPC, aborting!!!"
fi

if [[ ! -s $TESTDIR/$TESTFILE1 ]]; then
        log_fail "$TESTDIR/$TESTFILE1 was not created"
fi

#
# $DISK will be set if we're using slices on one disk
#
if [[ -n $DISK ]]; then
        log_must $ZPOOL add $TESTPOOL $POOLTYPE $DISK"s"$SLICE3 \
            $DISK"s"$SLICE4
else
        [[ -z $DISK2 || -z $DISK3 ]] && \
            log_unsupported "No spare disks available."
        log_must $ZPOOL add -f $TESTPOOL $POOLTYPE $DISK2"s"$SLICE \
	    $DISK3"s"$SLICE
fi

log_must $FILE_WRITE -o append -f $TESTDIR/$TESTFILE1 \
        -b $BLOCK_SIZE -c $SMALL_WRITE_COUNT -d 0

log_must $ZFS inherit compression $TESTPOOL/$TESTFS
log_pass "TESTPOOL mirror/raidz successfully grown"
