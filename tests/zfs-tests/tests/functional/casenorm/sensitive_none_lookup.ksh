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
# check that lookup succeeds only if (case=same and norm=same).
#
# STRATEGY:
# For each c/n name form:
# 1. Create file with given c/n name form.
# 2. Check that lookup fails for all other c/n name forms.

verify_runnable "global"

function cleanup
{
	destroy_testfs
}

log_onexit cleanup
log_assert "CS-not-UN FS: lookup succeeds only if using exact name form"

create_testfs "-o casesensitivity=sensitive -o normalization=none"

for name1 in $NAMES_ALL; do
	for name2 in $NAMES_ALL; do
		log_must create_file $name1
		if [[ $name2 != $name1 ]]; then
			log_mustnot lookup_file $name2
		fi
		delete_file $name1
	done
done

destroy_testfs

log_pass "CS-not-UN FS: lookup succeeds only if using exact name form"
