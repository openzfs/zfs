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
#	Verifies that tar will not include files attribute when @ flag is not
#	present.
#
# STRATEGY:
#	1. Create several files with attribute files.
#	2. Enter into directory A and record all files cksum
#	3. tar all the files to directory B.
#	4. Then tar the tar file to directory C.
#	5. Record all the files cksum in derectory C.
#	6. Verify the two records should be not identical.
#

verify_runnable "both"

log_assert "Verifies that tar will not include files attribute when @ flag is "\
	"not present"
log_onexit cleanup

set -A BEFORE_FCKSUM
set -A BEFORE_ACKSUM
set -A AFTER_FCKSUM
set -A AFTER_ACKSUM

for user in root $ZFS_ACL_STAFF1; do
	log_must set_cur_usr $user

	log_must create_files $TESTDIR

	#
	# Enter into initial directory and record all directory information,
	# then tar all the files to $TMP_DIR/files.tar.
	#
	tarout=$TMP_DIR/files.tar
	cd $INI_DIR
	log_must cksum_files $INI_DIR BEFORE_FCKSUM BEFORE_ACKSUM
	log_must usr_exec tar cpf $tarout *

	#
	# Enter into test directory and tar $TMP_DIR/files.tar to current
	# directory. Record all directory information and compare with initial
	# directory record.
	#
	cd $TST_DIR
	log_must usr_exec cp $tarout $TST_DIR
	log_must usr_exec tar xpf $tarout

	testfiles=$(ls -R $TST_DIR/*)
	typeset -i i=0
	while (( i < NUM_FILE )); do
		f=$(getitem $i $testfiles)
		ls_attr=$(ls -@ $f | awk '{print substr($1, 11, 1)}')
		if [[ $ls_attr == "@" ]]; then
			log_fail "extraction of attribute successful w/ -@ flag"
		fi

		(( i += 1 ))
	done

	log_must cksum_files $TST_DIR AFTER_FCKSUM AFTER_ACKSUM

	log_must compare_cksum BEFORE_FCKSUM AFTER_FCKSUM
	log_mustnot compare_cksum BEFORE_ACKSUM AFTER_ACKSUM

	log_must cleanup
done

log_pass "Verify tar without @ passed."
