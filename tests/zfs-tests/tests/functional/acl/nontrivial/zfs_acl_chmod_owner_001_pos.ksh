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
#	Verify that the write_owner for
#	owner/group/everyone are correct.
#
# STRATEGY:
# 1. Create file and  directory in zfs filesystem
# 2. Set special write_owner ACE to the file and directory
# 3. Try to chown/chgrp of the file and directory to take owner/group
# 4. Verify that the owner/group are correct. Follow these rules:
#	(1) If uid is granted the write_owner permission, then it can only do
#	    chown to its own uid, or a group that they are a member of.
#	(2) Owner will ignore permission of (1) even write_owner not granted.
#	(3) Superuser will always permit whatever they do.
#

verify_runnable "both"

function cleanup
{
	[[ -d $basedir ]] && rm -rf $basedir
	[[ -f $TESTDIR/$ARCHIVEFILE ]] && log_must rm -f $TESTDIR/$ARCHIVEFILE
	return 0
}

log_assert "Verify that the chown/chgrp could take owner/group " \
	"while permission is granted."
log_onexit cleanup

#
# Get the owner of a file/directory
#
function get_owner
{
	typeset node=$1

	if [[ -z $node ]]; then
		log_fail "node are not defined."
	fi

	echo $(ls -dl $node | awk '{print $3}')
}

#
# Get the group of a file/directory
#
function get_group
{
	typeset node=$1

	if [[ -z $node ]]; then
		log_fail "node are not defined."
	fi

	echo $(ls -dl $node | awk '{print $4}')
}


#
# Get the group name that a UID belongs to
#
function get_user_group
{
	typeset uid=$1
	typeset value

	if [[ -z $uid ]]; then
		log_fail "UID not defined."
	fi

	value=$(id $uid)

	if [[ $? -eq 0 ]]; then
		value=${value##*\(}
		value=${value%%\)*}
		echo $value
	else
		log_fail "Invalid UID (uid)."
	fi
}

function operate_node_owner
{
	typeset user=$1
	typeset node=$2
	typeset old_owner=$3
	typeset expect_owner=$4
	typeset ret new_owner

	if [[ $user == "" || $node == "" ]]; then
		log_fail "user, node are not defined."
	fi

	su $user -c "chown $expect_owner $node"
	ret=$?
	new_owner=$(get_owner $node)

	if [[ $new_owner != $old_owner ]]; then
		tar xpf $TESTDIR/$ARCHIVEFILE
	fi

	if [[ $ret -eq 0 ]]; then
		if [[ $new_owner != $expect_owner ]]; then
			log_note "Owner not changed as expected " \
				"($old_owner|$new_owner|$expect_owner), " \
				"but return code is $ret."
			return 1
		fi
	elif [[ $ret -ne 0 && $new_owner != $old_owner ]]; then
		log_note "Owner changed ($old_owner|$new_owner), " \
			"but return code is $ret."
		return 2
	fi

	return $ret
}

function operate_node_group
{
	typeset user=$1
	typeset node=$2
	typeset old_group=$3
	typeset expect_group=$4
	typeset ret new_group

	if [[ $user == "" || $node == "" ]]; then
		log_fail "user, node are not defined."
	fi

	su $user -c "chgrp $expect_group $node"
	ret=$?
	new_group=$(get_group $node)

	if [[ $new_group != $old_group ]]; then
		tar xpf $TESTDIR/$ARCHIVEFILE
	fi

	if [[ $ret -eq 0 ]]; then
		if [[ $new_group != $expect_group ]]; then
			log_note "Group not changed as expected " \
				"($old_group|$new_group|$expect_group), " \
				"but return code is $ret."
			return 1
		fi
	elif [[ $ret -ne 0 && $new_group != $old_group ]]; then
		log_note "Group changed ($old_group|$new_group), " \
			"but return code is $ret."
		return 2
	fi

	return $ret
}

function logname
{
	typeset acl_target=$1
	typeset user=$2
	typeset old=$3
	typeset new=$4
	typeset ret="log_mustnot"

	# To super user, read and write deny permission was override.
	if [[ $user == root ]]; then
		ret="log_must"
	elif [[ $user == $new ]] ; then
		if [[ $user == $old || $acl_target == *:allow ]]; then
			ret="log_must"
		fi
	fi

	print $ret
}

function check_chmod_results
{
	typeset user=$1
	typeset node=$2
	typeset flag=$3
	typeset acl_target=$3:$4
	typeset g_usr=$5
	typeset o_usr=$6
	typeset log old_owner old_group new_owner new_group

	old_owner=$(get_owner $node)
	old_group=$(get_group $node)

	if [[ $flag == "owner@" || $flag == "everyone@" ]]; then
		for new_owner in $user "nobody"; do
			new_group=$(get_user_group $new_owner)

			log=$(logname $acl_target $user \
				$old_owner $new_owner)

			$log operate_node_owner $user $node \
				$old_owner $new_owner

			$log operate_node_group $user $node \
				$old_group $new_group
		done
	fi
	if [[ $flag == "group@" || $flag == "everyone@" ]]; then
		for new_owner in $g_usr "nobody"; do
			new_group=$(get_user_group $new_owner)

			log=$(logname $acl_target $g_usr $old_owner \
				$new_owner)

			$log operate_node_owner $g_usr $node \
				$old_owner $new_owner

			$log operate_node_group $g_usr \
				$node $old_group $new_group
		done
	fi
	if [[ $flag == "everyone@" ]]; then
		for new_owner in $g_usr "nobody"; do
			new_group=$(get_user_group $new_owner)

			log=$(logname $acl_target $o_usr $old_owner \
				$new_owner)

			$log operate_node_owner $o_usr $node \
				$old_owner $new_owner

			$log operate_node_group $o_usr $node \
				$old_group $new_group
		done
	fi
}

function test_chmod_basic_access
{
	typeset user=$1
	typeset node=${2%/}
	typeset g_usr=$3
	typeset o_usr=$4
	typeset flag acl_t

	for flag in $a_flag; do
		for acl_t in $a_access; do
			log_must su $user -c "chmod A+$flag:$acl_t $node"

			tar cpf $TESTDIR/$ARCHIVEFILE basedir

			check_chmod_results $user $node $flag $acl_t $g_usr \
			    $o_usr

			log_must su $user -c "chmod A0- $node"
		done
	done
}

function setup_test_files
{
	typeset base_node=$1
	typeset user=$2
	typeset group=$3

	rm -rf $base_node

	log_must mkdir -p $base_node
	log_must chown $user:$group $base_node

	# Prepare all files/sub-dirs for testing.
	log_must su $user -c "touch $file"
	log_must su $user -c "chmod 444 $file"
	log_must su $user -c "mkdir -p $dir"
	log_must su $user -c "chmod 444 $dir"
	log_must su $user -c "chmod 555 $base_node"
}

typeset ARCHIVEFILE=archive.tar
typeset a_access="write_owner:allow write_owner:deny"
typeset a_flag="owner@ group@ everyone@"
typeset basedir="$TESTDIR/basedir"
typeset file="$basedir/file"
typeset dir="$basedir/dir"

cd $TESTDIR
setup_test_files $basedir 'root' 'root'
test_chmod_basic_access 'root' $file $ZFS_ACL_ADMIN  $ZFS_ACL_OTHER1
test_chmod_basic_access 'root' $dir $ZFS_ACL_ADMIN  $ZFS_ACL_OTHER1
rm -rf $basedir

setup_test_files $basedir $ZFS_ACL_STAFF1 $ZFS_ACL_STAFF_GROUP
test_chmod_basic_access $ZFS_ACL_STAFF1 $file $ZFS_ACL_STAFF2 $ZFS_ACL_OTHER1
test_chmod_basic_access $ZFS_ACL_STAFF1 $dir $ZFS_ACL_STAFF2 $ZFS_ACL_OTHER1
rm -rf $basedir

log_pass "Verify that the chown/chgrp could take owner/group " \
    "while permission is granted."
