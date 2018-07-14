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
# Copyright (c) 2012, 2016 by Delphix. All rights reserved.
# Copyright 2016 Nexenta Systems, Inc.
#

. $STF_SUITE/tests/functional/acl/acl_common.kshlib

# DESCRIPTION:
# Verify chmod have correct behaviour to directory and file not inherited
# when filesystem has the different aclinherit setting
#
# STRATEGY:
# 1. Use both super user and non-super user to run the test case.
# 2. Create basedir and a set of subdirectores and files inside of it.
# 3. For the following values of the aclinherity property, add ACEs with
#    different inherit options to basedir:
#    "discard", "noallow", "restricted" and "passthrough".
# 4. Create nested directories and files like the following.
#
#               ofile
#               odir
#    chmod -->  basedir -|
#                        |_ nfile1
#                        |_ ndir1 _
#                                  |_ nfile2
#                                  |_ ndir2 _
#                                            |_ nfile3
#                                            |_ ndir3
#
# 5. Verify non-inherited directories and files have the correct access
#    control capability.

verify_runnable "both"

function cleanup
{
	[[ -f $ofile ]] && log_must rm -f $ofile
	[[ -d $odir ]] && log_must rm -rf $odir
	[[ -d $basedir ]] && log_must rm -rf $basedir

	log_must zfs set aclmode=discard $TESTPOOL/$TESTFS
}

log_assert "Verify different inherit options combined with different" \
    "aclinherit property values"
log_onexit cleanup

# Define inherit flag
typeset aclinherit_flag=(discard noallow restricted passthrough)
typeset object_flag=(file_inherit dir_inherit file_inherit/dir_inherit)
typeset strategy_flag=("" inherit_only no_propagate inherit_only/no_propagate)

typeset ace_prefix1="user:$ZFS_ACL_OTHER1"
typeset ace_prefix2="user:$ZFS_ACL_OTHER2"

# Define the base directory and file
basedir=$TESTDIR/basedir;  ofile=$TESTDIR/ofile; odir=$TESTDIR/odir

# Define the files and directories will be created after chmod
ndir1=$basedir/ndir1; ndir2=$ndir1/ndir2; ndir3=$ndir2/ndir3
nfile1=$basedir/nfile1; nfile2=$ndir1/nfile2; nfile3=$ndir2/nfile3

# Verify all the node have expected correct access control
allnodes="$ndir1 $ndir2 $ndir3 $nfile1 $nfile2 $nfile3"

# According to inherited flag, verify subdirectories and files within it has
# correct inherited access control.
function verify_inherit #<aclinherit> <object> [strategy]
{
	# Define the nodes which will be affected by inherit.
	typeset non_inherit_nodes=""
	typeset inherit=$1
	typeset obj=$2
	typeset str=$3
	typeset inherit_type
	typeset str1="/inherit_only/inherited:"
	typeset str2="/inherited:"

	# count: the ACE item to fetch
	# passcnt: counter, if it achieves to maxaces,
	#	then no additional ACE should apply.
	# isinherit: indicate if the current target is in the inherit list.
	typeset -i count=0 pass=0 passcnt=0 isinherit=0 no_propagate=0

	log_must usr_exec mkdir -p $ndir3
	log_must usr_exec touch $nfile1 $nfile2 $nfile3

	# Get the inherit type/object_flag and non-inherited nodes.
	if [[ $obj == *"file_inherit"* && $obj == *"dir_inherit"* ]]; then
		inherit_type="both"
		if [[ $str == *"no_propagate"* ]]; then
			non_inherit_nodes= $ndir2 $ndir3 $nfile2 $nfile3
			no_propagate=1
		fi
	elif [[ $obj == *"dir_inherit"* ]]; then
		inherit_type="directory"
		non_inherit_nodes="$nfile1 $nfile2 $nfile3"
		if [[ $str == *"no_propagate"* ]]; then
			non_inherit_nodes="$non_inherit_nodes $ndir2 $ndir3"
			no_propagate=1
		fi
	else
		inherit_type="file"
		non_inherit_nodes="$ndir1 $ndir2 $ndir3"
		if [[ $str == *"no_propagate"* ]]; then
			non_inherit_nodes="$non_inherit_nodes $nfile2 $nfile3"
			no_propagate=1
		fi
	fi
	# Verify ACEs for all the dirs/files under basedir
	for node in $allnodes; do
		if [[ " $non_inherit_nodes " == *" $node "* ]]; then
			no_inherit=1
		else
			no_inherit=0
		fi
		i=0
		count=0
		passcnt=0
		while ((i < maxaces)); do
			typeset expacl

			if [[ $inherit == "restricted" ]]; then
				eval expacl=\$acls$i
			else
				eval expacl=\$acl$i
			fi
			case $inherit in
			noallow)
				if [[ $expacl == *":allow" ]]; then
					((i = i + 1))
					continue
				fi
				;;
			discard)
				((passcnt = maxaces))
				break
				;;
			esac
			if ((no_inherit == 0)); then
				((i = i + 1))
				continue
			fi

			if [[ -d $node ]]; then
				# Verify ACEs for subdirectory
				aclaction=${expacl##*:}
				acltemp=${expacl%:*}
				if [[ $inherit_type == "directory" ||
				    $inherit_type == "both" ]]; then
					expacl=${acltemp}${str2}
					expacl=${expacl}${aclaction}
				elif [[ $inherit_type == "file" ]]; then
					if [[ $expacl != *"inherit_only"* ]]; then
						# Directory should have
						# "inherit_only" appended
						expacl=${acltemp}${str1}
						expacl=${expacl}${aclaction}
					else
						expacl=${acltemp}${str2}
						expacl=${expacl}${aclaction}
					fi
				fi
				aclcur=$(get_ACE $node $count)
				aclcur=${aclcur#$count:}
				if [[ $no_propagate == 0 ]]; then
					if [[ $expacl != $aclcur ]]; then
						ls -vd $basedir
						ls -vd $node
						log_fail "$inherit $i #$count" \
						    "ACE: $aclcur," \
						    "expected: $expacl"
					fi
				else
					# Compare if directory has trivial ACL
					compare_acls $node $odir
					if [[ $? -ne 0 ]]; then
						ls -vd $basedir
						ls -vd $node
						log_fail "unexpected ACE:"
						    "$node, $inherit ($str)"
					fi
				fi
			# Verify ACE's for nested file
			elif [[ -f $node ]]; then
				compare_acls $node $ofile
				if [[ $? -ne 0 ]]; then
					ls -vd $basedir
					ls -vd $node
					log_fail "unexpected ACE:" \
					    "$node, $inherit ($str)"
				fi
			fi
			((count = count + 1))
			((i = i + 1))
		done

		# If there are no ACEs to be checked, compare the trivial ones.
		if ((passcnt == maxaces)); then
			if [[ -d $node ]]; then
				compare_acls $node $odir
			elif [[	-f $node ]]; then
				compare_acls $node $ofile
			fi

			if [[ $? -ne 0 ]]; then
				ls -vd $basedir
				ls -vd $node
				log_fail "Unexpected ACE: $node, $inherit ($str)"
			fi
		fi
	done
}

typeset -i i=0 maxaces=4
typeset acl0 acl1 acl2 acl3
typeset acls0 acls1 acls2 acls3

log_must zfs set aclmode=passthrough $TESTPOOL/$TESTFS

for inherit in "${aclinherit_flag[@]}"; do
	log_must zfs set aclinherit=$inherit $TESTPOOL/$TESTFS

	for user in root $ZFS_ACL_STAFF1; do
		log_must set_cur_usr $user

		for obj in "${object_flag[@]}"; do
			for str in "${strategy_flag[@]}"; do
				typeset inh_opt=$obj
				((${#str} != 0)) && inh_opt=$inh_opt/$str

				# Prepare 4 ACES, which should include:
				# deny -> to verify "noallow"
				# write_acl/write_owner -> to verify "restricted"
				acl0="${ace_prefix1}:read_xattr/write_acl"
				acl0="${acl0}/write_owner:${inh_opt}:deny"
				acl1="${ace_prefix2}:read_xattr/write_acl"
				acl1="${acl1}/write_owner:${inh_opt}:allow"
				acl2="${ace_prefix1}:read_xattr:${inh_opt}:deny"
				acl3="${ace_prefix2}:read_xattr:${inh_opt}:allow"

				# The ACEs filtered by write_acl/write_owner
				acls0=$acl0
				acls1="${ace_prefix2}:read_xattr"
				acls1="${acls1}:${inh_opt}:allow"
				acls2=$acl2
				acls3=$acl3
				#
				# Create basedir and tmp dir/file
				# for comparison.
				#
				log_note "$user: chmod $acl $basedir"
				log_must usr_exec mkdir $basedir
				log_must usr_exec mkdir $odir
				log_must usr_exec touch $ofile

				i=3
				while ((i >= 0)); do
					eval acl=\$acl$i
					log_must usr_exec chmod A+$acl $basedir
					((i = i - 1))
				done
				log_note "verify_inherit $inherit $obj $str"
				log_must verify_inherit $inherit $obj $str

				log_must usr_exec rm -rf $ofile $odir $basedir
			done
		done
	done
done

log_pass "Verify different inherit options combined with different" \
    "aclinherit property values"
