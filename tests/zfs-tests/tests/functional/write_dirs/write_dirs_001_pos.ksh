#!/bin/ksh -p
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
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Create as many directories with 50 big files each until the file system
# is full. The zfs file system should be stable and works well.
#
# STRATEGY:
# 1. Create a pool & dataset
# 2. Make directories in the zfs file system
# 3. Create 50 big files in each directories
# 4. Test case exit when the disk is full.
#

verify_runnable "both"

function cleanup
{
	destroy_dataset $TESTPOOL/$TESTFS
	wait_freeing $TESTPOOL
	sync_pool $TESTPOOL
	zfs create -o mountpoint=$TESTDIR $TESTPOOL/$TESTFS
}

typeset -i retval=0
log_assert "Creating directories with 50 big files in each, until file system "\
	"is full."

log_onexit cleanup

typeset -i bytes=8192
typeset -i num_writes=300000
typeset -i dirnum=50
typeset -i filenum=50

fill_fs "" $dirnum $filenum $bytes $num_writes
retval=$?
if (( retval == 28 )); then
	log_note "No space left on device."
elif (( retval != 0 )); then
	log_fail "Unexpected exit: $retval"
fi

log_pass "Write big files in a directory succeeded."
