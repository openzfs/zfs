#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# https://opensource.org/license/CDDL-1.0.
#

#
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/delegate/delegate_common.kshlib

#
# DESCRIPTION:
#	zfs unallow can handle invalid arguments.
#
# STRATEGY:
#	1. Set up basic test environment.
#	2. Verify zfs unallow handle invalid arguments correctly.
#

verify_runnable "both"

log_assert "zfs unallow can handle invalid arguments."
log_onexit restore_root_datasets

function neg_test
{
	log_mustnot eval "$@ >/dev/null 2>&1"
}

# Options that cause this test to fail:
# "-r"
set -A badopts "everyone -e" "everyone -u $STAFF1" "everyone everyone" \
	"-c -l" "-c -d" "-c -e" "-c -s" "-u -e" "-s -e" "-s -l -d" \
	"-s @non-exist-set -l" "-s @non-existen-set -d" \
	"-s @non-existen-set -e" "-r -u $STAFF1 $STAFF1" \
	"-u $STAFF1 -g $STAFF_GROUP" "-u $STAFF1 -e"

log_must setup_unallow_testenv

#
# The GNU getopt(3) implementation will reorder these arguments such the
# the parser can handle them and the test doesn't fail.  POSIXLY_CORRECT
# is set to disable the reordering so the original test cases will fail.
#
export POSIXLY_CORRECT=1

for dtst in $DATASETS ; do
	log_must zfs allow -c create $dtst

	typeset -i i=0
	while ((i < ${#badopts[@]})); do
		neg_test zfs unallow ${badopts[$i]} $dtst
		((i += 1))
	done

	# Causes test failure: neg_test user_run $STAFF1 zfs unallow $dtst
done

unset POSIXLY_CORRECT

log_pass "zfs unallow can handle invalid arguments passed."
