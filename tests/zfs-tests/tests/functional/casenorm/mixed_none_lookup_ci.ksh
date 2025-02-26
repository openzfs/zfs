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
# For the filesystem with casesensitivity=mixed, normalization=none,
# check that CI lookup succeeds only if (norm=same).
#
# STRATEGY:
# For each c/n name form:
# 1. Create file with given c/n name form.
# 2. Check that CI lookup succeeds if (norm=same).
# 3. Check that CI lookup fails if (norm=other).

verify_runnable "global"

function cleanup
{
	destroy_testfs
}

log_onexit cleanup
log_assert "CM-not-UN FS: CI lookup succeeds only if (norm=same)"

create_testfs "-o casesensitivity=mixed -o normalization=none"

for name1 in $NAMES_C ; do
	log_must create_file $name1
	for name2 in $NAMES_C ; do
		log_must lookup_file_ci $name2
	done
	for name2 in $NAMES_D; do
		log_mustnot lookup_file_ci $name2
	done
	delete_file $name1
done

for name1 in $NAMES_D ; do
	log_must create_file $name1
	for name2 in $NAMES_D ; do
		log_must lookup_file_ci $name2
	done
	for name2 in $NAMES_C; do
		log_mustnot lookup_file_ci $name2
	done
	delete_file $name1
done

destroy_testfs

log_pass "CM-not-UN FS: CI lookup succeeds only if (norm=same)"
