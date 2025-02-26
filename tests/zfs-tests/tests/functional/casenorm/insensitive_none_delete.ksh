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
# For the filesystem with casesensitivity=insensitive, normalization=none,
# check that delete succeeds if (norm=same).
#
# STRATEGY:
# For each c/n name form:
# 1. Create file with given c/n name form.
# 2. Check that delete succeeds if (norm=same).
# 3. Check that file is no longer accessible using any name form.
# 4. Check that delete fails if (norm=other).

verify_runnable "global"

function cleanup
{
	destroy_testfs
}

log_onexit cleanup
log_assert "CI-not-UN FS: delete succeeds if (norm=same)"

create_testfs "-o casesensitivity=insensitive -o normalization=none"

for name1 in $NAMES_C ; do
	for name2 in $NAMES_C ; do
		log_must create_file $name1
		log_must delete_file $name2
		log_mustnot lookup_any
	done
	for name2 in $NAMES_D ; do
		log_must create_file $name1
		log_mustnot delete_file $name2
		delete_file $name1
	done
done

for name1 in $NAMES_D ; do
	for name2 in $NAMES_D ; do
		log_must create_file $name1
		log_must delete_file $name2
		log_mustnot lookup_any
	done
	for name2 in $NAMES_C ; do
		log_must create_file $name1
		log_mustnot delete_file $name2
		delete_file $name1
	done
done

destroy_testfs

log_pass "CI-not-UN FS: delete succeeds if (norm=same)"
