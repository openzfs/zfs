#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# http://www.illumos.org/license/CDDL.
#

#
# Copyright 2015 Nexenta Systems, Inc.  All rights reserved.
#

. $STF_SUITE/tests/functional/casenorm/casenorm.kshlib

# DESCRIPTION:
# For the filesystem with casesensitivity=sensitive, normalization=none,
# check that delete succeeds only if (case=same and norm=same).
#
# STRATEGY:
# For each c/n name form:
# 1. Create file in given c/n name form.
# 2. Check that delete fails for all other name forms.
# 3. Check that delete succeeds if (case=same and norm=same).
# 4. Check that file is no longer accessible by any name form.

verify_runnable "global"

function cleanup
{
	destroy_testfs
}

log_onexit cleanup
log_assert "CS-not-UN FS: delete succeeds only if using exact name form"

create_testfs "-o casesensitivity=sensitive -o normalization=none"

for name1 in $NAMES_ALL ; do
	log_must create_file $name1
	for name2 in $NAMES_ALL ; do
		if [[ $name2 != $name1 ]] ; then
			log_mustnot delete_file $name2
		fi
	done
	delete_file $name1
	log_mustnot lookup_any
done

destroy_testfs

log_pass "CS-not-UN FS: delete succeeds only if using exact name form"
