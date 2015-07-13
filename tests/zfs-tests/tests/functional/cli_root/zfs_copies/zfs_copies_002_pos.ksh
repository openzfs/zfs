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
		destroy_dataset $TESTPOOL/fs_$val
	done
}

log_assert "Verify that the space used by multiple copies is charged correctly."
log_onexit cleanup

for val in 1 2 3; do
	log_must $ZFS create -o copies=$val $TESTPOOL/fs_$val

	log_must $MKFILE $FILESIZE /$TESTPOOL/fs_$val/$FILE
done

#
# Sync up the filesystem
#
$SYNC

#
# Verify 'zfs list' can correctly list the space charged
#
log_note "Verify 'zfs list' can correctly list the space charged."
fsize=${FILESIZE%[m|M]}
for val in 1 2 3; do
	used=$(get_used_prop $TESTPOOL/fs_$val)
	check_used $used $val
done

log_note "Verify 'ls -s' can correctly list the space charged."
for val in 1 2 3; do
	blks=`$LS -ls /$TESTPOOL/fs_$val/$FILE | $AWK '{print $1}'`
	if [[ -n "$LINUX" ]]; then
		(( used = blks * 1024 / (1024 * 1024) ))
	else
		(( used = blks * 512 / (1024 * 1024) ))
	fi
	check_used $used $val
done

typeset du_opt
if [[ -n "$LINUX" ]]; then
	df_opt="-t zfs"
else
	df_opt="-F zfs"
fi

log_note "Verify df(1M) can corectly display the space charged."
for val in 1 2 3; do
	used=`$DF -h /$TESTPOOL/fs_$val/$FILE | $GREP $TESTPOOL/fs_$val \
		| $AWK '{print $3}'`
	check_used $used $val
done

log_note "Verify du(1) can correctly display the space charged."
for val in 1 2 3; do
	used=`$DU $du_opt -h /$TESTPOOL/fs_$val/$FILE | $AWK '{print $1}'`
	# NOTE: On Linux, creating a sparse file won't show up correctly in list.
	check_used $used $val
done

log_pass "The space used by multiple copies is charged correctly as expected. "
