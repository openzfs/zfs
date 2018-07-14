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
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2012, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/acl/acl_common.kshlib

#
# DESCRIPTION:
#	Verify that explicit ACL setting to specified user or group will
#	override existed access rule.
#
# STRATEGY:
#	1. Loop root and non-root user.
#	2. Loop the specified access one by one.
#	3. Loop verify explicit ACL set to specified user and group.
#

verify_runnable "both"

function check_access #log user node access rflag
{
	typeset log=$1
	typeset user=$2
	typeset node=$3
	typeset access=$4
	typeset rflag=$5

	if [[ $rflag == "allow" && $access == execute ]]; then
		rwx_node $user $node $access
		#
		# When everyone@ were deny, this file can't execute.
		# So,'cannot execute' means user has the permission to
		# execute, just the file can't be execute.
		#
		if [[ $ZFS_ACL_ERR_STR == *"cannot execute"* ]]; then
			log_note "SUCCESS: rwx_node $user $node $access"
		else
			log_fail "FAIL: rwx_node $user $node $access"
		fi
	else
		$log rwx_node $user $node $access
	fi
}

function verify_explicit_ACL_rule #node access flag
{
	typeset node=$1
	typeset access=$2
	typeset flag=$3
	typeset log rlog rflag

	# Get the expect log check
	if [[ $flag == allow ]]; then
		log=log_mustnot
		rlog=log_must
		rflag=deny
	else
		log=log_must
		rlog=log_mustnot
		rflag=allow
	fi

	log_must usr_exec chmod A+everyone@:$access:$flag $node
	log_must usr_exec chmod A+user:$ZFS_ACL_OTHER1:$access:$rflag $node
	check_access $log $ZFS_ACL_OTHER1 $node $access $rflag
	log_must usr_exec chmod A0- $node

	log_must usr_exec \
		chmod A+group:$ZFS_ACL_OTHER_GROUP:$access:$rflag $node
	check_access $log $ZFS_ACL_OTHER1 $node $access $rflag
	check_access $log $ZFS_ACL_OTHER2 $node $access $rflag
	log_must usr_exec chmod A0- $node
	log_must usr_exec chmod A0- $node

	log_must usr_exec \
		chmod A+group:$ZFS_ACL_OTHER_GROUP:$access:$flag $node
	log_must usr_exec chmod A+user:$ZFS_ACL_OTHER1:$access:$rflag $node
	$log rwx_node $ZFS_ACL_OTHER1 $node $access
	$rlog rwx_node $ZFS_ACL_OTHER2 $node $access
	log_must usr_exec chmod A0- $node
	log_must usr_exec chmod A0- $node
}

log_assert "Verify that explicit ACL setting to specified user or group will" \
	"override existed access rule."
log_onexit cleanup

set -A a_access "read_data" "write_data" "execute"
set -A a_flag "allow" "deny"
typeset node

for user in root $ZFS_ACL_STAFF1; do
	log_must set_cur_usr $user

	log_must usr_exec touch $testfile
	log_must usr_exec mkdir $testdir
	log_must usr_exec chmod 755 $testfile $testdir

	for node in $testfile $testdir; do
		for access in ${a_access[@]}; do
			for flag in ${a_flag[@]}; do
				verify_explicit_ACL_rule $node $access $flag
			done
		done
	done

	log_must usr_exec rm -rf $testfile $testdir
done

log_pass "Explicit ACL setting to specified user or group will override " \
	"existed access rule passed."
