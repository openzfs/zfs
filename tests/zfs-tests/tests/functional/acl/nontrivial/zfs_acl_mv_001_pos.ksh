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
# Copyright (c) 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/acl/acl_common.kshlib

#
# DESCRIPTION:
# Verify that '/usr/bin/mv' supports ZFS ACL
#
# STRATEGY:
# 1. Create file and  directory in zfs filesystem
# 2. Set special ACE to the file and directory
# 3. Copy the file/directory within and across zfs file system
# 4. Verify that the ACL of file/directroy is not changed
#

verify_runnable "both"

function cleanup
{
	(( ${#cwd} != 0 )) && cd $cwd
	[[ -d $TESTDIR ]] && log_must rm -rf $TESTDIR/*
	[[ -d $TESTDIR1 ]] && log_must rm -rf $TESTDIR1
	(( ${#mask} != 0 )) && log_must umask $mask
}

function testing_mv #<flag for file|dir> <file1|dir1> <file2|dir2>
{
	typeset flag=$1
	set -A obj $2 $3
	typeset -i i=0
	typeset orig_acl=""
	typeset orig_mode=""
	typeset dst_acl=""
	typeset dst_mode=""

	if [[ $flag == "f" ]]; then
	while (( i < ${#obj[*]} ))
	do
		orig_acl="$(get_acl ${obj[i]})"
		orig_mode="$(get_mode ${obj[i]})"
		if (( i < 1 )); then
			log_must mv ${obj[i]} $dst_file
			dst_acl=$(get_acl $dst_file)
			dst_mode=$(get_mode $dst_file)
		else
			log_must mv ${obj[i]} $TESTDIR1
			dst_acl=$(get_acl $TESTDIR1/${obj[i]})
			dst_mode=$(get_mode $TESTDIR1/${obj[i]})
		fi

		if [[ "$dst_mode" != "$orig_mode" ]] || \
			[[ "$dst_acl" != "$orig_acl" ]]; then
			log_fail "mv fails to keep the acl for file."
		fi

		(( i = i + 1 ))
	done
	else
	while (( i < ${#obj[*]} ))
	do
		typeset orig_nested_acl=""
		typeset orig_nested_mode=""
		typeset dst_nested_acl=""
		typeset dst_nested_mode=""

		orig_acl=$(get_acl ${obj[i]})
		orig_mode=$(get_mode ${obj[i]})
		orig_nested_acl=$(get_acl ${obj[i]}/$nestedfile)
		orig_nested_mode=$(get_mode ${obj[i]}/$nestedfile)
		if (( i < 1 )); then
			log_must mv ${obj[i]} $dst_dir
			dst_acl=$(get_acl $dst_dir)
			dst_mode=$(get_mode $dst_dir)
			dst_nested_acl=$(get_acl $dst_dir/$nestedfile)
			dst_nested_mode=$(get_mode $dst_dir/$nestedfile)
		else
			log_must mv ${obj[i]} $TESTDIR1
			dst_acl=$(get_acl $TESTDIR1/${obj[i]})
			dst_mode=$(get_mode $TESTDIR1/${obj[i]})
			dst_nested_acl=$(get_acl \
				$TESTDIR1/${obj[i]}/$nestedfile)
			dst_nested_mode=$(get_mode \
				$TESTDIR1/${obj[i]}/$nestedfile)
		fi

		if [[ "$orig_mode" != "$dst_mode" ]] || \
		   [[ "$orig_acl" != "$dst_acl" ]] || \
		   [[ "$dst_nested_mode" != "$orig_nested_mode" ]] || \
		   [[ "$dst_nested_acl" != "$orig_nested_acl" ]]; then
			log_fail "mv fails to recursively keep the acl for " \
				"directory."
		fi

		(( i = i + 1 ))
	done
	fi
}

log_assert "Verify that 'mv' supports ZFS ACLs."

log_onexit cleanup

spec_ace="everyone@:execute:allow"
set -A orig_file "origfile1.$$" "origfile2.$$"
set -A orig_dir "origdir1.$$" "origdir2.$$"
nestedfile="nestedfile.$$"
dst_file=dstfile.$$
dst_dir=dstdir.$$
cwd=$PWD
mask=`umask`
umask 0022

#
# This assertion should only test 'mv' within the same filesystem
#
TESTDIR1=$TESTDIR/testdir1$$

[[ ! -d $TESTDIR1 ]] && \
	log_must mkdir -p $TESTDIR1

log_note "Create files and directories and set special ace on them for testing. "
cd $TESTDIR
typeset -i i=0
while (( i < ${#orig_file[*]} ))
do
	log_must touch ${orig_file[i]}
	log_must chmod A0+$spec_ace ${orig_file[i]}

	(( i = i + 1 ))
done
i=0
while (( i < ${#orig_dir[*]} ))
do
	log_must mkdir ${orig_dir[i]}
	log_must touch ${orig_dir[i]}/$nestedfile

	for obj in ${orig_dir[i]} ${orig_dir[i]}/$nestedfile; do
		log_must chmod A0+$spec_ace $obj
	done

	(( i = i + 1 ))
done

testing_mv "f" ${orig_file[0]} ${orig_file[1]}
testing_mv "d" ${orig_dir[0]} ${orig_dir[1]}

log_pass "'mv' succeeds to support ZFS ACLs."
