#!/usr/bin/ksh -p
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
# Copyright (c) 2013 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/interop/interop.cfg

#
# DESCRIPTION:
# Create a SVM device and add this to an existing ZFS pool
#
# STRATEGY:
# 1. Create a SVM metadevice
# 2. Create a ZFS file system
# 3. Add SVM metadevice to the ZFS pool
# 4. Create files and fill the pool.
#


function cleanup
{
	$RM -rf $TESTDIR/*
}

log_assert "Create a SVM device and add this to an existing ZFS pool"

log_onexit cleanup

# the current size of the test pool
typeset -i oldsize=`$ZFS get -pH -o value available $TESTPOOL`

log_must $ZPOOL add $TESTPOOL $META_DEVICE_PATH
log_must $ZPOOL iostat -v | $GREP $META_DEVICE_ID

# the size of the test pool after adding the extra device
typeset -i newsize=`$ZFS get -pH -o value available $TESTPOOL`

(( $oldsize >= $newsize )) && \
    log_fail "Pool space available ($oldsize) before adding a new device was "\
	     "larger than the space available ($newsize) afterwards."

log_note "Pool space available was ($oldsize), it's now ($newsize)"

typeset -i odirnum=1
typeset -i idirnum=0
typeset -i filenum=0
typeset -i retval=0
typeset bg=$TESTDIR/bigdirectory

fill_fs $bg 20 25 $FILE_SIZE $FILE_COUNT
retval=$?

afterwritepoolavail=`$ZFS get -pH -o value available $TESTPOOL`
readonly ENOSPC=28

(( $retval == $ENOSPC && $afterwritepoolavail < $oldsize)) && \
	log_pass "Successfully used ($(( $newsize - $oldsize )) bytes) in "\
		 "pool provided by SVM metadevice"

log_fail "Failed to use space in pool ($(( $newsize - $oldsize ))bytes) "\
	 "provided by SVM metadevice"
