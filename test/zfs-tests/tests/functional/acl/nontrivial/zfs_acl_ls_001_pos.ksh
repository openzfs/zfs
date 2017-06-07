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

. $STF_SUITE/tests/functional/acl/acl_common.kshlib

#
# DESCRIPTION:
# Verify that '/usr/bin/ls' command option supports ZFS ACL
#
# STRATEGY:
# 1. Create file and  directory in zfs filesystem
# 2. Verify that 'ls [-dv]' can list the ACEs of ACL of
#    file/directroy
# 3. Change the file/directory's acl
# 4. Verify that 'ls -l' can use the '+' to indicate the non-trivial
#    acl.
#

verify_runnable "both"

function cleanup
{
	(( ${#cwd} != 0 )) && cd $cwd
	[[ -d $TESTDIR ]] && log_must $RM -rf $TESTDIR/*
	(( ${#mask} != 0 )) && log_must $UMASK $mask
}

log_assert "Verify that '$LS' command supports ZFS ACLs."

log_onexit cleanup

file=$TESTFILE0
dir=dir.$$
cwd=$PWD
mask=`$UMASK`
spec_ace="everyone@:write_acl:allow"

$UMASK 0022

log_note "Create file and directory in the zfs filesystem. "
cd $TESTDIR
log_must $TOUCH $file
log_must $MKDIR $dir

log_note "Verify that '$LS [-dv]' can list file/directory ACEs of its acl."

typeset -i ace_num=0
for obj in $file $dir
do
	typeset ls_str=""
	if [[ -f $obj ]]; then
		ls_str="$LS -v"
	else
		ls_str="$LS -dv"
	fi

	for ace_type in "owner@" "group@" "everyone@"
	do
		$ls_str $obj | $GREP $ace_type > /dev/null 2>&1
		(( $? == 0 )) && (( ace_num += 1 ))
	done

	(( ace_num < 1 )) && \
		log_fail "'$LS [-dv] fails to list file/directroy acls."
done

log_note "Verify that '$LS [-dl] [-dv]' can output '+' to indicate " \
	"the acl existent."

for obj in $file $dir
do
	$CHMOD A0+$spec_ace $obj

	log_must eval "$LS -ld -vd $obj | $GREP "+" > /dev/null"
	log_must plus_sign_check_v $obj

	log_must eval "$LS -ld -vd $obj | $GREP $spec_ace > /dev/null"
	log_must plus_sign_check_l $obj
done

log_pass "'$LS' command succeeds to support ZFS ACLs."
