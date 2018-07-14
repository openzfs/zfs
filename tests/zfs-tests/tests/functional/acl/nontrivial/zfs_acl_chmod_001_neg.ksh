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
# Copyright (c) 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/acl/acl_common.kshlib

#
# DESCRIPTION:
#	Verify  1) Illegal options to chmod should fail.
#		2) Delete all the ACE will lead to fail.
#		3) Add ACE exceed 1024 will cause to fail.
#
# STRATEGY:
#	1. Loop root and non-root users
#	2. Verify all kinds of illegal option will lead to chmod failed.
#	3. Verify 'chmod A0-' will fail when try to delete all the ACE.
#	4. Verify 'chmod A+' will succeed when the ACE number exceed 1024.
#

verify_runnable "both"

log_assert "Verify illegal operating to ACL, it will fail."
log_onexit cleanup

function err_opts #node
{
	typeset A_opts="+A@ -A#- +A% =A^ =A# =A@ +A#\ asd \
			A+@ A-#- A+% A=^ A=# A=@ A+#"

	log_note "Illegal option to chmod should fail."
	for A in ${A_opts[@]}; do
		log_mustnot usr_exec chmod ${A}owner@:read_data:allow $node
		log_mustnot usr_exec chmod A+ asd owner@:execute:deny $node
	done

	typeset type_opts="everyone groups owner user@ users"
	for tp in ${type_opts[@]}; do
		log_mustnot usr_exec chmod A+$tp:read_data:deny $node
	done

	return 0
}

function del_all_ACE #node
{
	typeset node=$1
	typeset -i cnt

	cnt=$(count_ACE $node)
	while (( cnt > 0 )); do
		if (( cnt == 1 )); then
			log_mustnot chmod A0- $node
		else
			log_must chmod A0- $node
		fi

		(( cnt -= 1 ))
	done

	return 0
}

function exceed_max_ACE #node
{
	typeset node=$1
	typeset -i max=1024
	typeset -i cnt

	cnt=$(count_ACE $node)

	# One more ACE exceed the max limitation.
	(( max = max - cnt + 1 ))
	while (( max > 0 )); do
		if (( max == 1 )); then
			log_mustnot chmod A+owner@:read_data:allow $node
		else
			chmod A+owner@:read_data:allow $node
			if (($? != 0)); then
				((cnt = 1024 - max))
				log_fail "Add No.$cnt ACL item failed."
			fi
		fi

		(( max -= 1 ))
	done

	return 0
}

typeset node
typeset func_name="err_opts del_all_ACE exceed_max_ACE"

for usr in "root" "$ZFS_ACL_STAFF1"; do
	log_must set_cur_usr $usr

	for node in $testfile $testdir; do
		log_must usr_exec touch $testfile
		log_must usr_exec mkdir $testdir

		for func in $func_name; do
			log_must eval "$func $node"
		done

		log_must usr_exec rm -rf $testfile $testdir
	done
done

log_pass "Verify illegal operating to ACL passed."
