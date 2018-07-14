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
#	Verify directories include attribute in pax archive and restore with pax
#	should succeed.
#
# STRATEGY:
#	1. Use mktree create a set of directories in directory A.
#	2. Enter into directory A and record all directory information.
#	3. pax all the files to directory B.
#	4. Then pax the pax file to directory C.
#	5. Record all the directories informat in derectory C.
#	6. Verify the two records should be identical.
#

verify_runnable "both"

log_assert "Verify include attribute in pax archive and restore with pax " \
	"should succeed."
log_onexit cleanup

for user in root $ZFS_ACL_STAFF1; do
	log_must set_cur_usr $user

	[[ ! -d $INI_DIR ]] && log_must usr_exec mkdir -m 777 -p $INI_DIR
	log_must usr_exec mktree -b $INI_DIR -l 6 -d 2 -f 2

	#
	# Enter into initial directory and record all directory information,
	# then pax all the files to $TMP_DIR/files.pax.
	#
	[[ ! -d $TMP_DIR ]] && log_must usr_exec mkdir $TMP_DIR
	initout=$TMP_DIR/initout.$$
	paxout=$TMP_DIR/files.pax

	cd $INI_DIR
	log_must eval "record_cksum $INI_DIR $initout > /dev/null 2>&1"
	log_must eval "usr_exec pax -w -@ -f $paxout * > /dev/null 2>&1"

	#
	# Enter into test directory and pax $TMP_DIR/files.pax to current
	# directory. Record all directory information and compare with initial
	# directory record.
	#
	[[ ! -d $TST_DIR ]] && log_must usr_exec mkdir -m 777 $TST_DIR
	testout=$TMP_DIR/testout.$$
	cd $TST_DIR
	log_must eval "usr_exec pax -r -@ -f $paxout > /dev/null 2>&1"
	log_must eval "record_cksum $TST_DIR $testout > /dev/null 2>&1"

	log_must usr_exec diff $initout $testout

	log_must cleanup
done

log_pass "Directories pax archive and restre with pax passed."
