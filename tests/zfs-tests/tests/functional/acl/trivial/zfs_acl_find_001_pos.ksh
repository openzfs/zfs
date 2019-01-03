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
#	2. Verify all the specified files can be found with '-xattr',
#	3. Verify all the attribute files can be found with '-exec runat ls'
#

verify_runnable "both"

log_assert "Verifies ability to find files with attribute with" \
		"-xattr flag and using '-exec runat ls'"
log_onexit cleanup

for user in root $ZFS_ACL_STAFF1; do
	log_must set_cur_usr $user

	log_must create_files $TESTDIR
	initfiles=$(ls -R $INI_DIR/*)

	typeset -i i=0
	while (( i < NUM_FILE )); do
		f=$(getitem $i $initfiles)
		ff=$(usr_exec find $INI_DIR -type f -name ${f##*/} \
			-xattr -print)
		if [[ $ff != $f ]]; then
			log_fail "find file containing attribute fail."
		else
			log_note "find $f by '-xattr'."
		fi

		typeset -i j=0
		while (( j < NUM_ATTR )); do
			typeset af=attribute.$j
			fa=$(usr_exec find $INI_DIR -type f -name ${f##*/} \
				-xattr -exec runat {} ls $af \\\;)
			if [[ $fa != $af ]]; then
				log_fail "find file attribute fail"
			fi
			(( j += 1 ))
		done
		(( i += 1 ))
		log_note "find all attribute files of $f"
	done

	log_must cleanup
done

log_pass "find files with -xattr passed."
