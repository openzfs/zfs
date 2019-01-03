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
#

. $STF_SUITE/tests/functional/acl/acl_common.kshlib

#
# DESCRIPTION:
#	Verify chmod have correct behaviour to directory and file when
#	filesystem has the different aclinherit setting
#
# STRATEGY:
#	1. Loop super user and non-super user to run the test case.
#	2. Create basedir and a set of subdirectores and files within it.
#	3. Separately chmod basedir with different inherite options,
#	combine with the variable setting of aclinherit:
#		"discard", "noallow", "restricted" or "passthrough".
#	4. Then create nested directories and files like the following.
#
#                     ofile
#                     odir
#          chmod -->  basedir -|
#                              |_ nfile1
#                              |_ ndir1 _
#                                        |_ nfile2
#                                        |_ ndir2 _
#                                                  |_ nfile3
#                                                  |_ ndir3
#
#	5. Verify each directories and files have the correct access control
#	   capability.
#

verify_runnable "both"

function cleanup
{
	typeset dir

	# Cleanup basedir, compared file and dir.

	if [[ -f $ofile ]]; then
		log_must rm -f $ofile
	fi

	for dir in $odir $basedir ; do
		if [[ -d $dir ]]; then
			log_must rm -rf $dir
		fi
	done
	log_must zfs set aclmode=discard $TESTPOOL/$TESTFS
}

log_assert "Verify chmod have correct behaviour to directory and file when " \
	"filesystem has the different aclinherit setting."
log_onexit cleanup

# Define inherit flag
typeset aclinherit_flag=(discard noallow restricted passthrough)
typeset object_flag=(file_inherit dir_inherit file_inherit/dir_inherit)
typeset strategy_flag=("" inherit_only no_propagate inherit_only/no_propagate)

typeset ace_prefix1="user:$ZFS_ACL_OTHER1"
typeset ace_prefix2="user:$ZFS_ACL_OTHER2"

# Defile the based directory and file
basedir=$TESTDIR/basedir;  ofile=$TESTDIR/ofile; odir=$TESTDIR/odir

# Define the files and directories will be created after chmod
ndir1=$basedir/ndir1; ndir2=$ndir1/ndir2; ndir3=$ndir2/ndir3
nfile1=$basedir/nfile1; nfile2=$ndir1/nfile2; nfile3=$ndir2/nfile3

# Verify all the node have expected correct access control
allnodes="$ndir1 $ndir2 $ndir3 $nfile1 $nfile2 $nfile3"

#
# According to inherited flag, verify subdirectories and files within it has
# correct inherited access control.
#
function verify_inherit #<aclinherit> <object> [strategy]
{
	# Define the nodes which will be affected by inherit.
	typeset inherit_nodes
	typeset inherit=$1
	typeset obj=$2
	typeset str=$3
	typeset str1="/inherited:"

	# count: the ACE item to fetch
	# pass: to mark if the current ACE should apply to the target
	# maxnumber: predefine as 4
	# passcnt: counter, if it achieves to maxnumber,
	#	then no additional ACE should apply.
	# isinherit: indicate if the current target is in the inherit list.

	typeset -i count=0 pass=0 passcnt=0 isinherit=0 maxnumber=4

	log_must usr_exec mkdir -p $ndir3
	log_must usr_exec touch $nfile1 $nfile2 $nfile3

	# Get the files which inherited ACE.
	if [[ $obj == *"file_inherit"* ]]; then
		inherit_nodes="$inherit_nodes $nfile1"

		if [[ $str != *"no_propagate"* ]]; then
			inherit_nodes="$inherit_nodes $nfile2 $nfile3"
		fi
	fi
	# Get the directores which inherited ACE.
	if [[ $obj == *"dir_inherit"* ]]; then
		inherit_nodes="$inherit_nodes $ndir1"

		if [[ $str != *"no_propagate"* ]]; then
			inherit_nodes="$inherit_nodes $ndir2 $ndir3"
		fi
	fi

	for node in $allnodes; do
		if [[ " $inherit_nodes " == *" $node "* ]]; then
			isinherit=1
		else
			isinherit=0
		fi

		i=0
		count=0
		passcnt=0
		while ((i < maxnumber)); do
			pass=0
			eval expect1=\$acl$i

		#
		# aclinherit=passthrough,
		# inherit all inheritable ACL entries without any
		# modifications made to the ACL entries when they
		# are inherited.
		#
		# aclinherit=restricted,
		# any inheritable ACL entries will remove
		# write_acl and write_owner permissions when the ACL entry is
		# inherited.
		#
		# aclinherit=noallow,
		# only inherit inheritable ACE that specify "deny" permissions
		#
		# aclinherit=discard
		# will not inherit any ACL entries
		#

			case $inherit in
				passthrough)
					;;
				restricted)
					[[ $expect1 == *":allow" ]] && \
						eval expect1=\$acls$i
					;;
				noallow)
					if [[ $expect1 == *":allow" ]]; then
						pass=1
						((passcnt = passcnt + 1))
					fi
					;;
				discard)
					passcnt=maxnumber
					break
					;;
			esac
			propagate=0
			# verify ACE's only for inherited nodes
			if ((pass == 0 && isinherit == 1)); then
				aclaction=${expect1##*:}

				if [[ $expect1 == *"propagate"* ]]; then
					# strip strategy flags from the expect
					# value
					acltemp=${expect1%/*}
					expect1=${acltemp}
					propagate=1
				fi
				acltemp=${expect1%:*}
				if [[ -d $node ]]; then
					if [[ $expect1 == *"inherit_only"* \
					    && $propagate == 0 ]]; then
						# prepare expect value for
						# "inherit_only" nodes
						acltemp_subdir=${expect1%/*}
						expect1=${acltemp_subdir}${str1}
						expect1=${expect1}${aclaction}

					elif [[ $propagate == 1 ]]; then
						# prepare expect value for
						# "propagate" nodes
						expect1=${acltemp}:inherited:
						expect1=${expect1}${aclaction}

					else
						# prepare expect value for nodes
						# with no starategy flags
						expect1=${acltemp}${str1}
						expect1=${expect1}${aclaction}
					fi

				elif [[ -f $node ]]; then
					acltemp_subfile=${expect1%file*}
					expect1=${acltemp_subfile}inherited:
					expect1=${expect1}${aclaction}
				fi

				# Get the first ACE to do comparison

				aclcur=$(get_ACE $node $count)
				aclcur=${aclcur#$count:}
				if [[ -n $expect1 && $expect1 != $aclcur ]]; then
					ls -vd $basedir
					ls -vd $node
					log_fail "$inherit $i #$count " \
						"ACE: $aclcur, expect to be " \
						"$expect1"
				fi

				((count = count + 1))
			fi
			((i = i + 1))
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
				ls -vd $basedir
				ls -vd $node
				log_fail "Unexpect acl: $node, $inherit ($str)"
			fi
		fi
	done
}

typeset -i i=0
typeset acl0 acl1 acl2 acl3
typeset acls0 acls1 acls2 acls3

#
# Set aclmode=passthrough to make sure
# the acl will not change during chmod.
# A general testing should verify the combination of
# aclmode/aclinherit works well,
# here we just simple test them separately.
#

log_must zfs set aclmode=passthrough $TESTPOOL/$TESTFS

for inherit in "${aclinherit_flag[@]}"; do

	#
	# Set different value of aclinherit
	#

	log_must zfs set aclinherit=$inherit $TESTPOOL/$TESTFS

	for user in root $ZFS_ACL_STAFF1; do
		log_must set_cur_usr $user

		for obj in "${object_flag[@]}"; do
			for str in "${strategy_flag[@]}"; do
				typeset inh_opt=$obj
				((${#str} != 0)) && inh_opt=$inh_opt/$str

				#
				# Prepare 4 ACES, which should include :
				# deny -> to verify "noallow"
				# write_acl/write_owner -> to verify "restricted"
				#

				acl0=${ace_prefix1}":read_xattr/write_acl"
				acl0=${acl0}"/write_owner:$inh_opt:deny"
				acl1=${ace_prefix2}":read_xattr/write_acl/"
				acl1=${acl1}"write_owner:$inh_opt:allow"
				acl2="$ace_prefix1:read_xattr:$inh_opt:deny"
				acl3="$ace_prefix2:read_xattr:$inh_opt:allow"

				#
				# The ACE filtered by write_acl/write_owner
				#

				acls0="$ace_prefix1:read_xattr:$inh_opt:deny"
				acls1="$ace_prefix2:read_xattr:$inh_opt:allow"
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

				#
				# Place on a directory should succeed.
				#
					log_must usr_exec chmod A+$acl $basedir

					((i = i - 1))
				done

				verify_inherit $inherit $obj $str

				log_must usr_exec rm -rf $ofile $odir $basedir
			done
		done
	done
done

log_pass "Verify chmod inherit behaviour co-op with aclinherit setting passed."
