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

#
# Copyright (c) 2012, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/acl/acl_common.kshlib

#
# DESCRIPTION:
#	Verify chmod permission settings on files and directories, as both root
#	and non-root users.
#
# STRATEGY:
#	1. Loop root and $ZFS_ACL_STAFF1 as root and non-root users.
#	2. Create test file and directory in zfs filesystem.
#	3. Execute 'chmod' with specified options.
#	4. Check 'ls -l' output and compare with expect results.
#
# NOTE:
#	The test does not work for default "discard" aclmode property.
#	The test is modified to run with "passthrough" aclmode property.

verify_runnable "both"

function cleanup
{
	# reset aclmode=discard
	log_must zfs set aclmode=discard $TESTPOOL/$TESTFS
}

#	"init_map" "options" "expect_map"
set -A argv \
	"000" "a+rw"	"rw-rw-rw-"	"000" "a+rwx"	"rwxrwxrwx" \
	"000" "u+xr"	"r-x------"	"000" "gu-xw"	"---------" \
	"644" "a-r"	"-w-------"	"644" "augo-x"	"rw-r--r--" \
	"644" "=x"	"--x--x--x"	"644" "u-rw"	"---r--r--" \
	"644" "uo+x"	"rwxr--r-x"	"644" "ga-wr"	"---------" \
	"777" "augo+x"	"rwxrwxrwx"	"777" "go-xr"	"rwx-w--w-" \
	"777" "o-wx"	"rwxrwxr--"	"777" "ou-rx"	"-w-rwx-w-" \
	"777" "a+rwx"	"rwxrwxrwx"	"777" "u=rw"	"rw-rwxrwx" \
	"000" "123"	"--x-w--wx"	"000" "412"	"r----x-w-" \
	"231" "562"	"r-xrw--w-"	"712" "000"	"---------" \
	"777" "121"	"--x-w---x"	"123" "775"	"rwxrwxr-x"

log_assert " Verify chmod permission settings on files and directories"
log_onexit cleanup

#
# Verify file or directory have correct map after chmod
#
# $1 file or directory
#
function test_chmod_mapping #<file-dir>
{
	typeset node=$1
	typeset -i i=0

	while ((i < ${#argv[@]})); do
		usr_exec chmod ${argv[i]} $node
		if (($? != 0)); then
			log_note "usr_exec chmod ${argv[i]} $node"
			return 1
		fi
		usr_exec chmod ${argv[((i + 1))]} $node
		if (($? != 0)); then
			log_note "usr_exec chmod ${argv[((i + 1))]} $node"
			return 1
		fi

		typeset mode
		mode=$(get_mode ${node})

		if [[ $mode != "-${argv[((i + 2))]}"* && \
			$mode != "d${argv[((i + 2))]}"* ]]
		then
			log_fail "FAIL: '${argv[i]}' '${argv[((i + 1))]}' \
				'${argv[((i + 2))]}'"
		fi

		((i += 3))
	done

	return 0
}

# set aclmode=passthrough
log_must zfs set aclmode=passthrough $TESTPOOL/$TESTFS

for user in root $ZFS_ACL_STAFF1; do
	log_must set_cur_usr $user

	# Test file
	log_must usr_exec touch $testfile
	log_must test_chmod_mapping $testfile
	log_must usr_exec chmod A+user:$ZFS_ACL_STAFF2:write_acl:allow $testfile

	# Test directory
	log_must usr_exec mkdir $testdir
	log_must test_chmod_mapping $testdir
	log_must usr_exec chmod A+user:$ZFS_ACL_STAFF2:write_acl:allow $testdir

	# Grant privileges of write_acl and retest the chmod commands.

	log_must set_cur_usr $ZFS_ACL_STAFF2
	log_must test_chmod_mapping $testfile
	log_must test_chmod_mapping $testdir

	log_must set_cur_usr $user

	log_must usr_exec rm $testfile
	log_must usr_exec rm -rf $testdir
done

log_pass "Setting permissions using 'chmod' completed successfully."
