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
# Verify that 'find' command with '-ls' and '-acl' options supports ZFS ACL
#
# STRATEGY:
# 1. Create 5 files and 5 directories in zfs filesystem
# 2. Select a file or directory and add a few ACEs to it
# 3. Use find -ls to check the "+" existen only with the selected file or
#    directory
# 4. Use find -acl to check only the selected file/directory in the list
#

verify_runnable "both"

function cleanup
{
	[[ -d $TESTDIR ]] && rm -rf $TESTDIR/*
	(( ${#cmd} != 0 )) && cd $cwd
	(( ${#mask} != 0 )) && umask $mask
}

function find_ls_acl #<opt> <obj>
{
	typeset opt=$1 # -ls or -acl
	typeset obj=$2
	typeset rst_str=""

	if [[ $opt == "ls" ]]; then
		rst_str=`find . -ls | grep "+" | awk '{print $11}'`
	else
		rst_str=`find . -acl`
	fi

	if [[ $rst_str == "./$obj" ]]; then
		return 0
	else
		return 1
	fi
}

log_assert "Verify that 'find' command supports ZFS ACLs."

log_onexit cleanup

set -A ops " A+user:$ZFS_ACL_STAFF1:read_data:allow" \
	" A+user:$ZFS_ACL_STAFF1:write_data:allow"

f_base=testfile.$$ # Base file name for tested files
d_base=testdir.$$ # Base directory name for tested directory
cwd=$PWD
mask=`umask`

log_note "Create five files and directories in the zfs filesystem. "
cd $TESTDIR
umask 0777
typeset -i i=0
while ((i < 5))
do
	log_must touch ${f_base}.$i
	log_must mkdir ${d_base}.$i

	((i = i + 1))
done

for obj in ${f_base}.3 ${d_base}.3
do
	i=0
	while ((i < ${#ops[*]}))
	do
		log_must chmod ${ops[i]} $obj

		((i = i + 1))
	done

	for opt in "ls" "acl"
	do
		log_must find_ls_acl $opt $obj
	done

	log_note "Check the file access permission according to the added ACEs"
	if [[ ! -r $obj || ! -w $obj ]]; then
		log_fail "The added ACEs for $obj cannot be represented in " \
			"mode."
	fi

	log_note "Remove the added ACEs from ACL."
	i=0
	while ((i < ${#ops[*]}))
	do
		log_must chmod A0- $obj

		((i = i + 1))
	done
done

log_pass "'find' command succeeds to support ZFS ACLs."
