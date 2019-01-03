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
#	chmod A{+|-|=} read_data|write_data|execute for owner@ group@ or
#	everyone@ correctly alters mode bits .
#
# STRATEGY:
#	1. Loop root and non-root user.
#	2. Get the random initial map.
#	3. Get the random ACL string.
#	4. Separately chmod +|-|= read_data|write_data|execute
#	5. Check map bits
#

verify_runnable "both"

log_assert "chmod A{+|-|=} read_data|write_data|execute for owner@, group@ " \
	"or everyone@ correctly alters mode bits."
log_onexit cleanup

set -A bits 0 1 2 3 4 5 6 7
set -A a_flag owner group everyone
set -A a_access read_data write_data execute
set -A a_type allow deny

#
# Get a random item from an array.
#
# $1 the base set
#
function random_select #array_name
{
	typeset arr_name=$1
	typeset -i ind

	eval typeset -i cnt=\${#${arr_name}[@]}
	(( ind = $RANDOM % cnt ))

	eval print \${${arr_name}[$ind]}
}

#
# Create a random string according to array name, the item number and
# separated tag.
#
# $1 array name where the function get the elements
# $2 the items number which you want to form the random string
# $3 the separated tag
#
function form_random_str #<array_name> <count> <sep>
{
	typeset arr_name=$1
	typeset -i count=${2:-1}
	typeset sep=${3:-""}

	typeset str=""
	while (( count > 0 )); do
		str="${str}$(random_select $arr_name)${sep}"

		(( count -= 1 ))
	done

	print $str
}

#
# According to the original bits, the input ACE access and ACE type, return the
# expect bits after 'chmod A0{+|=}'.
#
# $1 bits which was make up of three bit 'rwx'
# $2 ACE access which is read_data, write_data or execute
# $3 ACE type which is allow or deny
#
function cal_bits #bits acl_access acl_type
{
	typeset bits=$1
	typeset acl_access=$2
	typeset acl_type=$3
	set -A bit r w x

	typeset tmpbits=""
	typeset -i i=0
	while (( i < 3 )); do
		if [[ $acl_access == *"${a_access[i]}"* ]]; then
			if [[ $acl_type == "allow" ]]; then
				tmpbits="$tmpbits${bit[i]}"
			elif [[ $acl_type == "deny" ]]; then
				tmpbits="${tmpbits}-"
			fi
		else
			tmpbits="$tmpbits${bits:$i:1}"
		fi

		(( i += 1 ))
	done

	echo "$tmpbits"
}

#
# Based on the initial node map before chmod and the ace-spec, check if chmod
# has the correct behaven to map bits.
#
function check_test_result #init_mode node acl_flag acl_access a_type
{
	typeset init_mode=$1
	typeset node=$2
	typeset acl_flag=$3
	typeset acl_access=$4
	typeset acl_type=$5

	typeset -L3 u_bits=$init_mode
	typeset g_bits=${init_mode:3:3}
	typeset -R3 o_bits=$init_mode

	if [[ $acl_flag == "owner" || $acl_flag == "everyone" ]]; then
		u_bits=$(cal_bits $u_bits $acl_access $acl_type)
	fi
	if [[ $acl_flag == "group" || $acl_flag == "everyone" ]]; then
		g_bits=$(cal_bits $g_bits $acl_access $acl_type)
	fi
	if [[ $acl_flag == "everyone" ]]; then
		o_bits=$(cal_bits $o_bits $acl_access $acl_type)
	fi

	typeset cur_mode=$(get_mode $node)
	cur_mode=${cur_mode:1:9}

	if [[ $cur_mode == $u_bits$g_bits$o_bits ]]; then
		log_note "SUCCESS: Current map($cur_mode) == " \
			"expected map($u_bits$g_bits$o_bits)"
	else
		log_fail "FAIL: Current map($cur_mode) != " \
			"expected map($u_bits$g_bits$o_bits)"
	fi
}

function test_chmod_map #<node>
{
	typeset node=$1
	typeset init_mask acl_flag acl_access acl_type
	typeset -i cnt

	if (( ${#node} == 0 )); then
		log_fail "FAIL: file name or directory name is not defined."
	fi

	# Get the initial map
	init_mask=$(form_random_str bits 3)
	# Get ACL flag, access & type
	acl_flag=$(form_random_str a_flag)
	(( cnt = ($RANDOM % ${#a_access[@]}) + 1 ))
	acl_access=$(form_random_str a_access $cnt '/')
	acl_access=${acl_access%/}
	acl_type=$(form_random_str a_type)

	typeset acl_spec=${acl_flag}@:${acl_access}:${acl_type}

	# Set the initial map and back the initial ACEs
	typeset orig_ace=/tmp/orig_ace.$$
	typeset cur_ace=/tmp/cur_ace.$$

	for operator in "A0+" "A0="; do
		log_must usr_exec chmod $init_mask $node
		init_mode=$(get_mode $node)
		init_mode=${init_mode:1:9}
		log_must usr_exec eval "ls -vd $node > $orig_ace"

		# To "A=", firstly add one ACE which can't modify map
		if [[ $operator == "A0=" ]]; then
			log_must chmod A0+user:$ZFS_ACL_OTHER1:execute:deny \
				$node
		fi
		log_must usr_exec chmod $operator$acl_spec $node
		check_test_result \
			$init_mode $node $acl_flag $acl_access $acl_type

		# Check "chmod A-"
		log_must usr_exec chmod A0- $node
		log_must usr_exec eval "ls -vd $node > $cur_ace"

		if diff $orig_ace $cur_ace; then
			log_note "SUCCESS: current ACEs are equal to " \
				"original ACEs. 'chmod A-' succeeded."
		else
			log_fail "FAIL: 'chmod A-' failed."
		fi
	done

	[[ -f $orig_ace ]] && log_must usr_exec rm -f $orig_ace
	[[ -f $cur_ace ]] && log_must usr_exec rm -f $cur_ace
}

for user in root $ZFS_ACL_STAFF1; do
	set_cur_usr $user

	typeset -i loop_cnt=20
	while (( loop_cnt > 0 )); do
		log_must usr_exec touch $testfile
		test_chmod_map $testfile
		log_must rm -f $testfile

		log_must usr_exec mkdir $testdir
		test_chmod_map $testdir
		log_must rm -rf $testdir

		(( loop_cnt -= 1 ))
	done
done

log_pass "chmod A{+|-|=} read_data|write_data|execute for owner@, group@ " \
	"or everyone@ correctly alters mode bits passed."
