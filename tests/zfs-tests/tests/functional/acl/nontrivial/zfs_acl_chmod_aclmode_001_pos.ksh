#!/usr/bin/ksh -p
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
# Copyright (c) 2012, 2016 by Delphix. All rights reserved.
# Copyright 2016 Nexenta Systems, Inc.
#


. $STF_SUITE/tests/functional/acl/acl_common.kshlib

# DESCRIPTION:
# Verify chmod have correct behaviour on directories and files when
# filesystem has the different aclmode setting
#
# STRATEGY:
# 1. Loop super user and non-super user to run the test case.
# 2. Create basedir and a set of subdirectores and files within it.
# 3. Separately chmod basedir with different aclmode options,
#    combine with the variable setting of aclmode:
#    "discard", "groupmask", or "passthrough".
# 4. Verify each directories and files have the correct access control
#    capability.

verify_runnable "both"

function cleanup
{
	(( ${#cwd} != 0 )) && cd $cwd

	[[ -f $TARFILE ]] && log_must rm -f $TARFILE
	[[ -d $basedir ]] && log_must rm -rf $basedir
}

log_assert "Verify chmod have correct behaviour to directory and file when" \
    "filesystem has the different aclmode setting"
log_onexit cleanup

set -A aclmode_flag "discard" "groupmask" "passthrough"

set -A ace_prefix \
    "user:$ZFS_ACL_OTHER1" \
    "user:$ZFS_ACL_OTHER2" \
    "group:$ZFS_ACL_STAFF_GROUP" \
    "group:$ZFS_ACL_OTHER_GROUP"

set -A argv "000" "444" "644" "777" "755" "231" "562" "413"

set -A ace_file_preset \
    "read_data" \
    "write_data" \
    "append_data" \
    "execute" \
    "read_data/write_data" \
    "read_data/write_data/append_data" \
    "write_data/append_data" \
    "read_data/execute" \
    "write_data/append_data/execute" \
    "read_data/write_data/append_data/execute"

# Define the base directory and file
basedir=$TESTDIR/basedir;  ofile=$basedir/ofile; odir=$basedir/odir
nfile=$basedir/nfile; ndir=$basedir/ndir

TARFILE=$TESTDIR/tarfile

# Verify all the node have expected correct access control
allnodes="$nfile $ndir"

# According to the original bits, the input ACE access and ACE type, return the
# expect bits after 'chmod A0{+|=}'.
#
# $1 isdir indicate if the target is a directory
# $2 bits which was make up of three bit 'rwx'
# $3 bits_limit which was make up of three bit 'rwx'
# $4 ACE access which is read_data, write_data or execute
# $5 ctrl which is to determine allow or deny according to owner/group bit
function cal_bits # isdir bits bits_limit acl_access ctrl
{
	typeset -i isdir=$1
	typeset -i bits=$2
	typeset -i bits_limit=$3
	typeset acl_access=$4
	typeset -i ctrl=${5:-0}
	typeset flagr=0 flagw=0 flagx=0
	typeset tmpstr

	if (( ctrl == 0 )); then
		if (( (( bits & 4 )) != 0 )); then
			flagr=1
		fi
		if (( (( bits & 2 )) != 0 )); then
			flagw=1
		fi
		if (( (( bits & 1 )) != 0 )); then
			flagx=1
		fi
	else
		# Determine ACE as per owner/group bit
		flagr=1
		flagw=1
		flagx=1

		if (( ((bits & 4)) != 0 )) && \
			(( ((bits_limit & 4)) != 0 )); then
			flagr=0
		fi
		if (( ((bits & 2)) != 0 )) && \
			(( ((bits_limit & 2)) != 0 )); then
			flagw=0
		fi
		if (( ((bits & 1)) != 0 )) && \
			(( ((bits_limit & 1)) != 0 )); then
			flagx=0
		fi
	fi

	if ((flagr != 0)); then
		if [[ $acl_access == *"read_data"* ]]; then
			if [[ $acl_access == *"allow"* &&
			    $passthrough == 0 ]]; then
				tmpstr=${tmpstr}
			elif ((isdir == 0)); then
				tmpstr=${tmpstr}/read_data
			else
				tmpstr=${tmpstr}/list_directory/read_data
			fi
		fi
	fi

	if ((flagw != 0)); then
		if [[ $acl_access == *"allow"* && $passthrough == 0 ]]; then
			tmpstr=${tmpstr}
		else
			if [[ $acl_access == *"write_data"* ]]; then
				if ((isdir == 0)); then
					tmpstr=${tmpstr}/write_data
				else
					tmpstr=${tmpstr}/add_file/write_data
				fi
			fi
			if [[ $acl_access == *"append_data"* ]]; then
				if ((isdir == 0)); then
					tmpstr=${tmpstr}/append_data
				else
					tmpstr=${tmpstr}/add_subdirectory
					tmpstr=${tmpstr}/append_data
				fi
			fi
		fi
	fi

	if ((flagx != 0)); then
		if [[ $acl_access == *"execute"* ]]; then
			if [[ $acl_access == *"allow"* &&
			    $passthrough == 0 ]]; then
				tmpstr=${tmpstr}
			else
				tmpstr=${tmpstr}/execute
			fi
		fi
	fi

	tmpstr=${tmpstr#/}

	echo "$tmpstr"
}

#
# To translate an ace if the node is dir
#
# $1 isdir indicate if the target is a directory
# $2 acl to be translated
#
function translate_acl # isdir acl
{
	typeset -i isdir=$1
	typeset acl=$2
	typeset who prefix acltemp action

	if ((isdir != 0)); then
		who=${acl%%:*}
		prefix=$who
		acltemp=${acl#*:}
		acltemp=${acltemp%%:*}
		prefix=$prefix:$acltemp
		action=${acl##*:}
		acl=$prefix:$(cal_bits $isdir 7 7 $acl 0):$action
	fi
	echo "$acl"
}

#
# To verify if a new ACL is generated as result of
# chmod operation.
#
# $1 bit indicates whether owner/group bit
# $2 newmode indicates the mode changed using chmod
# $3 isdir indicate if the target is a directory
#
function check_new_acl # bit newmode isdir
{
	typeset bits=$1
	typeset mode=$2
	typeset -i isdir=$3
	typeset new_acl
	typeset gbit
	typeset ebit
	typeset str=":"
	typeset dc=""

	gbit=${mode:1:1}
	ebit=${mode:2:1}
	if (( ((bits & 4)) == 0 )); then
		if (( ((gbit & 4)) != 0 || \
		    ((ebit & 4)) != 0 )); then
			if ((isdir == 0)); then
				new_acl=${new_acl}${str}read_data
			else
				new_acl=${new_acl}${str}list_directory/read_data
			fi
			str="/"
		fi
	fi
	if (( ((bits & 2)) == 0 )); then
		if (( ((gbit & 2)) != 0 || \
		    ((ebit & 2)) != 0 )); then
			if ((isdir == 0)); then
				new_acl=${new_acl}${str}write_data/append_data
			else
				new_acl=${new_acl}${str}add_file/write_data/
				new_acl=${new_acl}add_subdirectory/append_data
				dc="/delete_child"
			fi
			str="/"
		fi
	fi
	if (( ((bits & 1)) == 0 )); then
		if (( ((gbit & 1)) != 0 || \
		    ((ebit & 1)) != 0 )); then
				new_acl=${new_acl}${str}execute
		fi
	fi
	new_acl=${new_acl}${dc}
	echo "$new_acl"
}

function build_new_acl # newmode isdir
{
	typeset newmode=$1
	typeset isdir=$2
	typeset expect
	if ((flag == 0)); then
		prefix="owner@"
		bit=${newmode:0:1}
		status=$(check_new_acl $bit $newmode $isdir)

	else
		prefix="group@"
		bit=${newmode:1:1}
		status=$(check_new_acl $bit $newmode $isdir)
	fi
	expect=$prefix$status:deny
	echo $expect
}

# According to inherited flag, verify subdirectories and files within it has
# correct inherited access control.
function verify_aclmode # <aclmode> <node> <newmode>
{
	# Define the nodes which will be affected by inherit.
	typeset aclmode=$1
	typeset node=$2
	typeset newmode=$3

	# count: the ACE item to fetch
	# pass: to mark if the current ACE should apply to the target
	# passcnt: counter, if it achieves to maxnumber,
	#	then no additional ACE should apply.

	typeset -i count=0 pass=0 passcnt=0
	typeset -i bits=0 obits=0 bits_owner=0 isdir=0
	typeset -i total_acl
	typeset -i acl_count=$(count_ACE $node)

	((total_acl = maxnumber + 3))

	if [[ -d $node ]]; then
		((isdir = 1))
	fi

	((i = maxnumber - 1))
	count=0
	passcnt=0
	flag=0
	while ((i >= 0)); do
		pass=0
		expect1=${acls[$i]}
		passthrough=0
		#
		# aclmode=passthrough,
		# no changes will be made to the ACL other than
		# generating the necessary ACL entries to represent
		# the new mode of the file or directory.
		#
		# aclmode=discard,
		# delete all ACL entries that don't represent
		# the mode of the file.
		#
		# aclmode=groupmask,
		# reduce user or group permissions.  The permissions are
		# reduced, such that they are no greater than the group
		# permission bits, unless it is a user entry that has the
		# same UID as the owner of the file or directory.
		# Then, the ACL permissions are reduced so that they are
		# no greater than owner permission bits.
		#

		case $aclmode in
		passthrough)
			if ((acl_count > total_acl)); then
				expect1=$(build_new_acl $newmode $isdir)
				flag=1
				((total_acl = total_acl + 1))
				((i = i + 1))
			else
				passthrough=1
				expect1=$(translate_acl $isdir $expect1)
			fi
			;;
		groupmask)
			if ((acl_count > total_acl)); then
				expect1=$(build_new_acl $newmode $isdir)
				flag=1
				((total_acl = total_acl + 1))
				((i = i + 1))
			elif [[ $expect1 == *":allow"* ]]; then
				who=${expect1%%:*}
				aclaction=${expect1##*:}
				prefix=$who
				acltemp=""
				reduce=0
				# To determine the mask bits
				# according to the entry type.
				#
				case $who in
				owner@)
					pos=0
					;;
				group@)
					pos=1
					;;
				everyone@)
					pos=2
					;;
				user)
					acltemp=${expect1#*:}
					acltemp=${acltemp%%:*}
					owner=$(get_owner $node)
					group=$(get_group $node)
					if [[ $acltemp == $owner ]]; then
						pos=0
					else
						pos=1
					fi
					prefix=$prefix:$acltemp
					;;
				group)
					acltemp=${expect1#*:}
					acltemp=${acltemp%%:*}
					pos=1
					prefix=$prefix:$acltemp
					reduce=1
					;;
				esac

				obits=${newmode:$pos:1}
				((bits = $obits))
				# permission should be no greater than the
				# group permission bits
				if ((reduce != 0)); then
					((bits &= ${newmode:1:1}))
					# The ACL permissions are reduced so
					# that they are no greater than owner
					# permission bits.
					((bits_owner = ${newmode:0:1}))
					((bits &= $bits_owner))
				fi

				if ((bits < obits)) && [[ -n $acltemp ]]; then
					expect2=$prefix:
					new_bit=$(cal_bits $isdir $obits \
					    $bits_owner $expect1 1)
					expect2=${expect2}${new_bit}:allow
				else
					expect2=$prefix:
					new_bit=$(cal_bits $isdir $obits \
					    $obits $expect1 1)
					expect2=${expect2}${new_bit}:allow
				fi

				priv=$(cal_bits $isdir $obits $bits_owner \
				    $expect2 0)
				expect1=$prefix:$priv:$aclaction
			else
				expect1=$(translate_acl $isdir $expect1)
			fi
			;;
		discard)
			passcnt=maxnumber
			break
			;;
		esac

		if ((pass == 0)) ; then
			# Get the first ACE to do comparison
			aclcur=$(get_ACE $node $count)
			aclcur=${aclcur#$count:}
			if [[ -n $expect1 && $expect1 != $aclcur ]]; then
				ls -vd $node
				log_fail "$aclmode $i #$count " \
					"ACE: $aclcur, expect to be " \
					"$expect1"
			fi
		((count = count + 1))
		fi
		((i = i - 1))
	done

	#
	# If there's no any ACE be checked, it should be identify as
	# an normal file/dir, verify it.
	#
	if ((passcnt == maxnumber)); then
		if [[ -d $node ]]; then
			compare_acls $node $odir
		elif [[	-f $node ]]; then
			compare_acls $node $ofile
		fi

		if [[ $? -ne 0 ]]; then
			ls -vd $node
			log_fail "Unexpect acl: $node, $aclmode ($newmode)"
		fi
	fi
}



typeset -i maxnumber=0
typeset acl
typeset target
typeset -i passthrough=0
typeset -i flag=0

for mode in "${aclmode_flag[@]}"; do
	log_must zfs set aclmode=$mode $TESTPOOL/$TESTFS

	for user in root $ZFS_ACL_STAFF1; do
		log_must set_cur_usr $user

		log_must usr_exec mkdir $basedir

		log_must usr_exec mkdir $odir
		log_must usr_exec touch $ofile
		log_must usr_exec mkdir $ndir
		log_must usr_exec touch $nfile

		for obj in $allnodes; do
			maxnumber=0
			for preset in "${ace_file_preset[@]}"; do
				for prefix in "${ace_prefix[@]}"; do
					acl=$prefix:$preset

					case $((maxnumber % 2)) in
					0)
						acl=$acl:deny
						;;
					1)
						acl=$acl:allow
						;;
					esac

					log_must usr_exec chmod A+$acl $obj
					acls[$maxnumber]=$acl

					((maxnumber = maxnumber + 1))
				done
			done
			# Archive the file and directory
			log_must tar cpf@ $TARFILE $basedir

			if [[ -d $obj ]]; then
				target=$odir
			elif [[ -f $obj ]]; then
				target=$ofile
			fi
			for newmode in "${argv[@]}"; do
				log_must usr_exec chmod $newmode $obj
				log_must usr_exec chmod $newmode $target
				log_must verify_aclmode $mode $obj $newmode
				log_must tar xpf@ $TARFILE
			done
		done

		log_must usr_exec rm -rf $basedir $TARFILE
	done
done

log_pass "Verify chmod behaviour co-op with aclmode setting passed"
