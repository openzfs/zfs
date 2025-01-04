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
# For the filesystem with casesensitivity=mixed, normalization=formD,
# check that lookup succeeds only if (case=same).
#
# STRATEGY:
# For each c/n name form:
# 1. Create file with given c/n name form.
# 2. Check that lookup succeeds if (case=same).
# 3. Check that lookup fails if (case=other).

verify_runnable "global"

function cleanup
{
	destroy_testfs
}

log_onexit cleanup
log_assert "CM-UN FS: lookup succeeds if (case=same)"

create_testfs "-o casesensitivity=mixed -o normalization=formD"

for name1 in $NAMES_ALL; do
	log_must create_file $name1
	for name2 in $NAMES_ALL; do
		if [[ $(get_case $name2) == $(get_case $name1) ]]; then
			log_must lookup_file $name2
		else
			log_mustnot lookup_file $name2
		fi
	done
	delete_file $name1
done

destroy_testfs

log_pass "CM-UN FS: lookup succeeds if (case=same)"
