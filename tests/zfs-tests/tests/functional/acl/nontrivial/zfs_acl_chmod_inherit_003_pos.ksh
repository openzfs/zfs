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
# Copyright 2016 Nexenta Systems, Inc.
#

. $STF_SUITE/tests/functional/acl/acl_common.kshlib

# DESCRIPTION:
# Verify chmod have correct behaviour to directory and file when
# filesystem has the different aclinherit setting
#
# STRATEGY:
# 1. Use both super user and non-super user to run the test case.
# 2. Create basedir and a set of subdirectores and files within it.
# 3. Separately chmod basedir with different inherite options,
#    combine with the variable setting of aclinherit:
#    "discard", "noallow", "restricted" or "passthrough".
# 4. Then create nested directories and files like the following.
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
# 5. Verify each directories and files have the correct access control
#    capability.

verify_runnable "both"

function cleanup
{
	[[ -f $ofile ]] && log_must rm -f $ofile
	[[ -d $odir ]] && log_must rm -rf $odir
	[[ -d $basedir ]] && log_must rm -rf $basedir
}

log_assert "Verify chmod have correct behaviour to directory and file when" \
    "filesystem has the different aclinherit setting"
log_onexit cleanup

# Define inherit flag
typeset aclinherit_flag=("discard" "noallow" "restricted" "passthrough")
typeset object_flag=("f-" "-d" "fd")
typeset strategy_flag=("--" "i-" "-n" "in")

typeset ace_prefix1="owner@"
typeset ace_prefix2="group@"
typeset ace_prefix3="everyone@"

# Define the base directory and file
basedir=$TESTDIR/basedir; ofile=$TESTDIR/ofile; odir=$TESTDIR/odir

# Define the files and directories that will be created after chmod
ndir1=$basedir/ndir1; ndir2=$ndir1/ndir2; ndir3=$ndir2/ndir3
nfile1=$basedir/nfile1; nfile2=$ndir1/nfile2; nfile3=$ndir2/nfile3

# Verify all nodes have expected correct access control
allnodes="$ndir1 $ndir2 $ndir3 $nfile1 $nfile2 $nfile3"

# According to inherited flag, verify subdirectories and files within it has
# correct inherited access control.
function verify_inherit #<aclinherit> <object> [strategy]
{
	# Define the nodes which will be affected by inherit.
	typeset inherit_nodes
	typeset inherit=$1
	typeset obj=$2
	typeset str=$3

	log_must usr_exec mkdir -p $ndir3
	log_must usr_exec touch $nfile1 $nfile2 $nfile3

	# Check if we have any inheritance flags set
	if [[ $obj != "--" ]]; then
		# Files should have inherited ACEs only if file_inherit is set
		if [[ ${obj:0:1} == "f" ]]; then
			inherit_nodes="$inherit_nodes $nfile1"
			if [[ ${str:1:1} != "n" ]]; then
				inherit_nodes="$inherit_nodes $nfile2 $nfile3"
			fi
		fi

		# Directories should have inherited ACEs if file_inherit without
		# no_propagate and/or dir_inherit is set
		if [[ (${obj:0:1} == "f" && ${str:1:1} != "n") ||
		    ${obj:1:1} == "d" ]]; then
			inherit_nodes="$inherit_nodes $ndir1"
			if [[ ${str:1:1} != "n" ]]; then
				inherit_nodes="$inherit_nodes $ndir2 $ndir3"
			fi
		fi
	fi

	for node in $allnodes; do
		typeset -i i=0 count=0 inherited=0
		typeset expacl perm inh act

		if [[ "$inherit_nodes" == *"$node"* ]]; then
			inherited=1
		fi

		while ((i < $maxaces)); do
			# If current node isn't in inherit list, there's
			# nothing to check, skip to checking trivial ACL
			if ((inherited == 0)); then
				((count = maxaces + 1))
				break
			fi

			eval expacl=\$acl$i
			case $inherit in
			discard)
				# Do not inherit any ACEs
				((count = maxaces + 1))
				break
				;;
			noallow)
				# Only inherit inheritable ACEs that specify
				# "deny" permissions
				if [[ $expacl == *":allow" ]] ; then
					((i = i + 1))
					continue
				fi
				;;
			restricted)
				# Remove write_acl and write_owner permissions
				# when the ACEs is inherited
				eval expacl=\$acls$i
				;;
			passthrough)
				;;
			esac

			perm=${expacl%:*}
			inh=${perm##*:}
			inh=${inh:0:2}
			perm=${perm%:*}
			act=${expacl##*:}

			if [[ -d $node ]]; then
				# Clear inheritance flags if no_propagate is set
				if [[ ${str:1:1} == "n" ]]; then
					inh="--"
				fi
				expacl="$perm:$inh"
				# Set inherit_only if there's a file_inherit
				# without dir_inherit
				if [[ ${obj:0:1} == "f" &&
				    ${obj:1:1} != "d" ]]; then
					expacl="${expacl}i---I:$act"
				else
					expacl="${expacl}----I:$act"
				fi
			elif [[ -f $node ]] ; then
				expacl="$perm:------I:$act"
			fi

			aclcur=$(get_ACE $node $count compact)
			aclcur=${aclcur#$count:}
			if [[ -n $expacl && $expacl != $aclcur ]]; then
				ls -Vd $basedir
				ls -Vd $node
				log_fail "$inherit $i #$count" \
				    "expected: $expacl, current: $aclcur"
			fi

			((i = i + 1))
			((count = count + 1))
		done

		# There were no non-trivial ACEs to check, do the trivial ones
		if ((count == maxaces + 1)); then
			if [[ -d $node ]]; then
				compare_acls $node $odir
			elif [[ -f $node ]]; then
				compare_acls $node $ofile
			fi

			if [[ $? -ne 0 ]]; then
				ls -Vd $basedir
				ls -Vd $node
				log_fail "unexpected acl: $node," \
				    "$inherit ($str)"
			fi
		fi

	done
}

typeset -i i=0 maxaces=6
typeset acl0 acl1 acl2 acl3 acl4 acl5
typeset acls0 acls1 acls2 acls3 acls4 acls5

log_must zfs set aclmode=passthrough $TESTPOOL/$TESTFS

for inherit in "${aclinherit_flag[@]}"; do
	log_must zfs set aclinherit=$inherit $TESTPOOL/$TESTFS

	for user in root $ZFS_ACL_STAFF1; do
		log_must set_cur_usr $user

		for obj in "${object_flag[@]}"; do
			for str in "${strategy_flag[@]}"; do
				typeset inh_opt=$obj
				((${#str} != 0)) && inh_opt="${inh_opt}${str}--"

				inh_a="${inh_opt}-"
				inh_b="${inh_opt}I"

				# deny - to verify "noallow"
				# write_acl/write_owner - to verify "restricted"
				acl0="$ace_prefix1:-------A-W-Co-:$inh_a:allow"
				acl1="$ace_prefix2:-------A-W-Co-:$inh_a:deny"
				acl2="$ace_prefix3:-------A-W-Co-:$inh_a:allow"
				acl3="$ace_prefix1:-------A-W----:$inh_a:deny"
				acl4="$ace_prefix2:-------A-W----:$inh_a:allow"
				acl5="$ace_prefix3:-------A-W----:$inh_a:deny"

				# ACEs filtered by write_acl/write_owner
				acls0="$ace_prefix1:-------A-W----:$inh_b:allow"
				acls1="$ace_prefix2:-------A-W-Co-:$inh_b:deny"
				acls2="$ace_prefix3:-------A-W----:$inh_b:allow"
				acls3="$ace_prefix1:-------A-W----:$inh_b:deny"
				acls4="$ace_prefix2:-------A-W----:$inh_b:allow"
				acls5="$ace_prefix3:-------A-W----:$inh_b:deny"

				log_must usr_exec mkdir $basedir
				log_must usr_exec mkdir $odir
				log_must usr_exec touch $ofile

				((i = maxaces - 1))
				while ((i >= 0)); do
					eval acl=\$acl$i
					log_must usr_exec chmod A+$acl $basedir
					((i = i - 1))
				done

				verify_inherit $inherit $obj $str

				log_must usr_exec rm -rf $ofile $odir $basedir
			done
		done
	done
done

log_pass "Verify chmod inherit behaviour co-op with aclinherit setting passed"
