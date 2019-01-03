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
#	Verifies ability to find files with attribute with -xattr flag and using
#	"-exec runat ls".
#
# STRATEGY:
#	1. In directory A, create several files and add attribute files for them
#	2. Delete all the attribute files.
#	2. Verify all the specified files can not be found with '-xattr',
#	3. Verify all the attribute files can not be found with '-exec runat ls'
#

verify_runnable "both"

log_assert "verifies -xattr doesn't include files without " \
		"attribute and using '-exec runat ls'"
log_onexit cleanup

for user in root $ZFS_ACL_STAFF1; do
	log_must set_cur_usr $user

	log_must create_files $TESTDIR

	initfiles=$(ls -R $INI_DIR/*)
	typeset -i i=0
	while (( i < NUM_FILE )); do
		f=$(getitem $i $initfiles)
		usr_exec runat $f rm attribute*
		(( i += 1 ))
	done

	i=0
	while (( i < NUM_FILE )); do
		f=$(getitem $i $initfiles)
		ff=$(usr_exec find $INI_DIR -type f -name ${f##*/} \
			-xattr -print)
		if [[ $ff == $f ]]; then
			log_fail "find not containing attribute should fail."
		fi

		typeset -i j=0
		while (( j < NUM_ATTR )); do
			fa=$(usr_exec find $INI_DIR -type f -name ${f##*/} \
				-xattr -exec runat {} ls attribute.$j \\\;)
			if [[ $fa == attribute.$j ]]; then
				log_fail "find file attribute should fail."
			fi
			(( j += 1 ))
		done
		log_note "Failed to find $f and its attribute file as expected."

		(( i += 1 ))
	done

	log_must cleanup
done

log_pass "find files which have no attrabute files with -xattr passed."
