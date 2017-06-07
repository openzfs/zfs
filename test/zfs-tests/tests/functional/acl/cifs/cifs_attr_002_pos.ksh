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
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2012 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/acl/acl_common.kshlib
. $STF_SUITE/tests/functional/acl/cifs/cifs.kshlib

#
# DESCRIPTION:
#	Verify the user with PRIV_FILE_FLAG_SET/PRIV_FILE_FLAG_CLEAR
#	could set/clear BSD'ish attributes.
#	(Immutable, nounlink, and appendonly)
#
# STRATEGY:
#	1. Loop super user and non-super user to run the test case.
#	2. Create basedir and a set of subdirectores and files within it.
#	3. Grant user has PRIV_FILE_FLAG_SET/PRIV_FILE_FLAG_CLEAR separately.
#	4. Verify set/clear BSD'ish attributes should succeed.
#

verify_runnable "global"

function cleanup
{
	$RM -f $mntpt/file $mntpt/dir || log_fail
	    "$($LS -d/ c $mntpt/file $mntpt/dir)"

	log_must $CP $orig_user_attr /etc/user_attr
	log_must $RM -f $orig_user_attr
}

function try
{
	typeset obj=$1		# The file or dir to operate on
	typeset attr=$2		# The attribute to set or clear
	typeset user=$3		# The user to run the command as
	typeset priv=$4		# What privilege to run with if non-root
	typeset op=$5		# Whether to set or clear the attribute

	typeset cmd="$CHMOD $op$attr $obj"

	#
	# No one can add 'q' (av_quarantine) to a directory. root can do
	# anything else. A regular user can remove no attributes without the
	# 'all' privilege, and can add attributes (other than 'q' on a
	# directory) with the 'file_flag_set' or 'all' privileges.
	#
	if [[ $user == 'root' ]]; then
		if [[ $attr =~ 'q' && -d $obj && $op == $add ]]; then
			log_mustnot $cmd
		else
			log_must $cmd
		fi
	else
		if [[ $attr =~ 'q' && -d $obj && $op == $add ]]; then
			log_mustnot $SU $user -c "$cmd"
		else
			if [[ $op == $add ]]; then
				if [[ -n $priv ]]; then
					log_must $SU $user -c "$cmd"
				else
					log_mustnot $SU $user -c "$cmd"
				fi
			else
				if [[ $attr = 'q' && -d $obj ]]; then
					log_must $SU $user -c "$cmd"
				elif [[ $priv =~ 'all' ]]; then
					log_must $SU $user -c "$cmd"
				else
					log_mustnot $SU $user -c "$cmd"
					#
					# Remove the attribute, so the next
					# iteration starts with a known state.
					#
					log_must $cmd
				fi
			fi
		fi
	fi


	# Can't add av_quarantine to a directory, so don't check for that
	[[ $attr == 'q' && $op == $add && -d $obj ]] && return
	chk_attr $op $obj $attr
}

function chk_attr
{
	typeset op=$1
	typeset obj=$2
	typeset attr=$3

	# Extract the attribute string - just the text inside the braces
	typeset attrstr="$($LS -d/ c $obj | $SED '1d; s/.*{\(.*\)}.*/\1/g')"

	if [[ $op == $add ]]; then
		[[ $attrstr =~ $attr ]] || log_fail "$op $attr -> $attrstr"
	else
		[[ $attrstr =~ $attr ]] && log_fail "$op $attr -> $attrstr"
	fi
}

#
# Grant the privset to the given user
#
# $1: The given user
# $2: The given privset
#
function grant_priv
{
	typeset user=$1
	typeset priv=$2

	if [[ -z $user || -z $priv ]]; then
		log_fail "User($user), Priv($priv) not defined."
	fi

	priv_mod=",$priv"

	# If we're root, don't modify /etc/user_attr
	[[ $user == 'root' ]] && return 0

	$ECHO "$user::::type=normal;defaultpriv=basic$priv_mod" >> \
	    /etc/user_attr
	return $?
}

#
# Revoke the all additional privset from the given user
#
# $1: The given user
#
function reset_privs
{
	typeset user=$1

	if [[ -z $user ]]; then
		log_fail "User not defined."
	fi

	priv_mod=

	$CP $orig_user_attr /etc/user_attr || log_fail "Couldn't modify user_attr"
	return 0
}

log_assert "Verify set/clear BSD'ish attributes will succeed while user has " \
    "file_flag_set or all privilege"
log_onexit cleanup

add='S+c'
del='S-c'
mntpt=$(get_prop mountpoint $TESTPOOL/$TESTFS)
orig_user_attr="/tmp/user_attr.$$"
attributes="u i a d q m"

log_must $CP /etc/user_attr $orig_user_attr

for owner in root $ZFS_ACL_STAFF1 $ZFS_ACL_STAFF2; do
	$TOUCH $mntpt/file || log_fail "Failed to create $mntpt/file"
	$MKDIR $mntpt/dir || log_fail "Failed to mkdir $mntpt/dir"
	$CHOWN $owner $mntpt/file $mntpt/dir || log_fail "Failed to chown file"
	for user in 'root' $ZFS_ACL_STAFF2; do
		for attr in $attributes; do
			for priv in 'file_flag_set' 'all'; do
				log_note "Trying $owner $user $attr $priv"
				grant_priv $user $priv
				try $mntpt/file $attr $user $priv $add
				try $mntpt/file $attr $user $priv $del
				try $mntpt/dir $attr $user $priv $add
				try $mntpt/dir $attr $user $priv $del
				reset_privs $user
			done
		done
	done
	$RM -rf $mntpt/file $mntpt/dir || log_fail \
	    "$($LS -d/ c $mntpt/file $mntpt/dir)"
done

log_pass "Set/Clear BSD'ish attributes succeed while user has " \
    "PRIV_FILE_FLAG_SET/PRIV_FILE_FLAG_CLEAR privilege"
