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
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

# Copyright (c) 2012, 2016 by Delphix. All rights reserved.
# Copyright 2016 Nexenta Systems, Inc.
#

. $STF_SUITE/tests/functional/acl/acl_common.kshlib

# DESCRIPTION:
# Verify aclinherit=passthrough-x will inherit the execute permission only if
# file creation mode requests it.
#
# STRATEGY:
# 1. Use both super user and non-super user to run the test case.
# 2. Set aclinherit=passthrough-x
# 3. Create basedir and a set of files, one with 644 and one with 755 mode.
# 4. Verify that execute permission is inherited only if file creation mode
#    requests them.

verify_runnable "both"

function cleanup
{
	[[ -d $basedir ]] && log_must rm -rf $basedir
}

log_assert "aclinherit=passthrough-x should inherit the execute permission" \
    "only if file creation mode requests it"
log_onexit cleanup

set -A aces \
    "owner@:rwxp:f:allow" \
    "group@:rxp:f:allow" \
    "everyone@:rxp:f:allow"

typeset basedir="$TESTDIR/basedir"
typeset nfile1="$basedir/nfile1" nfile2="$basedir/nfile2"

function check_execute_bit
{
	typeset ace
	typeset file=$1
	typeset -i i=0

	while ((i < 6)); do
		ace=$(get_ACE $file $i)
		if [[ "$ace" == *"execute"* ]]; then
			return 0
		fi
		((i = i + 1))
	done

	return 1
}

function verify_inherit
{
	typeset -i i=0

	log_must usr_exec mkdir $basedir

	# Modify owner@, group@ and everyone@ ACEs to include execute
	# permission (see above), and make them file-inheritable
	while ((i < ${#aces[*]})); do
		log_must usr_exec chmod A$i=${aces[i]} $basedir
		((i = i + 1))
	done

	# Create file with 644 mode
	log_must usr_exec touch $nfile1
	# Check that execute permission wasn't inherited
	log_mustnot check_execute_bit $nfile1

	# Use cp(1) to copy over /usr/bin/true
	log_must usr_exec cp /usr/bin/true $nfile2
	# Check that execute permission was inherited
	log_must check_execute_bit $nfile2
}

log_must zfs set aclmode=passthrough $TESTPOOL/$TESTFS
log_must zfs set aclinherit=passthrough-x $TESTPOOL/$TESTFS

for user in root $ZFS_ACL_STAFF1; do
	log_must set_cur_usr $user
	verify_inherit
	cleanup
done

log_pass "aclinherit=passthrough-x should inherit the execute permission" \
    "only if file creation mode requests it"
