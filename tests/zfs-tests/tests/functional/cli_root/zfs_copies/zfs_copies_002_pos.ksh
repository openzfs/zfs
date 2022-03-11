#!/bin/ksh
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
# Copyright (c) 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zfs_copies/zfs_copies.kshlib

#
# DESCRIPTION:
#	Verify that the space used by multiple copies is charged correctly
#
# STRATEGY:
#	1. Create filesystems with copies set as 2,3 respectively;
#	2. Copy specified size data into each filesystem;
#	3. Verify that the space is charged as expected with zfs list, ls -s, df(1m),
#	   du(1) commands;
#

verify_runnable "both"

function cleanup
{
	typeset val

	for val in 1 2 3; do
		datasetexists $TESTPOOL/fs_$val && \
			destroy_dataset $TESTPOOL/fs_$val
	done
}

log_assert "Verify that the space used by multiple copies is charged correctly."
log_onexit cleanup

for val in 1 2 3; do
	log_must zfs create -o compression=off -o copies=$val $TESTPOOL/fs_$val

	log_must mkfile $FILESIZE /$TESTPOOL/fs_$val/$FILE
done

#
# Sync up the filesystem
#
sync_all_pools

#
# Verify 'zfs list' can correctly list the space charged
#
log_note "Verify 'zfs list' can correctly list the space charged."
fsize=${FILESIZE%[m|M]}
for val in 1 2 3; do
	used=$(get_prop used $TESTPOOL/fs_$val)
	check_used $used $val
done

log_note "Verify 'ls -s' can correctly list the space charged."
if is_linux || is_freebsd; then
	blksize=1024
else
	blksize=512
fi
for val in 1 2 3; do
	blks=`ls -ls /$TESTPOOL/fs_$val/$FILE | awk '{print $1}'`
	(( used = blks * $blksize )) # bytes
	check_used $used $val
done

log_note "Verify df(1) can correctly display the space charged."
for val in 1 2 3; do
	if is_freebsd; then
		used=`df -m /$TESTPOOL/fs_$val | awk -v pa=$TESTPOOL/fs_$val -v fs=fs_$val '$0 ~ pa && $4 ~ fs {print $3}'`
	else
		used=`df -F zfs -k /$TESTPOOL/fs_$val/$FILE | awk -v pa=$TESTPOOL/fs_$val '$0 ~ pa {print $3}'`
		(( used = used * 1024 )) # kb -> bytes
	fi
	check_used $used $val
done

log_note "Verify du(1) can correctly display the space charged."
for val in 1 2 3; do
	if is_freebsd; then
		used=`du -h /$TESTPOOL/fs_$val/$FILE | awk '{print $1}'`
	else
		used=`du -k /$TESTPOOL/fs_$val/$FILE | awk '{print $1}'`
		(( used = used * 1024 )) # kb -> bytes
	fi
	check_used $used $val
done

log_pass "The space used by multiple copies is charged correctly as expected. "
