#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or https://opensource.org/licenses/CDDL-1.0.
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
