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
#	Verifies that cp will not be able to include file attribute when
#	attribute is unreadable (unless the user is root)
#
# STRATEGY:
#	1. In directory A, create several files and add attribute files for them
#	2. chmod all files'the attribute files to '000'.
#	3. Implement 'cp -@p' to files.
#	4. Verify attribute files are not existing for non-root user.
#

verify_runnable "both"

log_assert "Verifies that cp won't be able to include file attribute when " \
	"attribute is unreadable (except root)"
log_onexit cleanup

function test_unreadable_attr
{
	typeset initfiles=$(ls -R $INI_DIR/*)

	typeset -i i=0
	while (( i < NUM_FILE )); do
		typeset f=$(getitem $i $initfiles)
		typeset -i j=0
		while (( j < NUM_ATTR )); do
			# chmod all the attribute files to '000'.
			usr_exec runat $f chmod 000 attribute.$j

			(( j += 1 ))
		done

		#
		# Implement 'cp -@p' to the file whose attribute files
		# models are '000'.
		#
		usr_exec cp -@p $f $TST_DIR > /dev/null 2>&1

		typeset testfiles=$(ls -R $TST_DIR/*)
		typeset tf=$(getitem $i $testfiles)
		typeset ls_attr=$(usr_exec ls -@ $tf | \
			awk '{print substr($1, 11, 1)}')

		case $ZFS_ACL_CUR_USER in
		root)
			case $ls_attr in
			@)
				log_note "SUCCESS: root enable to cp attribute"\
					"when attribute files is unreadable"
				break ;;
			*)
				log_fail "root should enable to cp attribute " \
					"when attribute files is unreadable"
				break ;;
			esac
			;;
		$ZFS_ACL_STAFF1)
			case $ls_attr in
			@)
				log_fail "non-root shouldn't enable to cp " \
					"attribute when attribute files is " \
					"unreadable."
				break ;;
			*)
				log_note "SUCCESS: non-root doesn't enable to "\
					"cp attribute when attribute files is "\
					"unreadable."
				break ;;
			esac
			;;
		*)
		esac


		(( i += 1 ))
	done
}

for user in root $ZFS_ACL_STAFF1; do
	log_must set_cur_usr $user

	log_must create_files $TESTDIR
	test_unreadable_attr

	log_must cleanup
done

log_pass "'cp -@p' won't include file attribute passed."
