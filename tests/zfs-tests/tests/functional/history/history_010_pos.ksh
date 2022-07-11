#!/bin/ksh -p
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
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/history/history_common.kshlib

#
# DESCRIPTION:
#	Verify internal long history information are correct.
#
# STRATEGY:
#	1. Create non-root test user and group.
#	2. Do some zfs operations as a root and non-root user.
#	3. Verify the long history information is correct.
#

verify_runnable "global"

function cleanup
{
	del_user $HIST_USER
	del_group $HIST_GROUP
	datasetexists $root_testfs && destroy_dataset $root_testfs -rf
}

log_assert "Verify internal long history information are correct."
log_onexit cleanup

root_testfs=$TESTPOOL/$TESTFS1

# Create history test group and user and get user id and group id
add_group $HIST_GROUP
add_user $HIST_GROUP $HIST_USER

#
# Verify the test user can execute the zfs utilities.  This may not
# be possible due to default permissions on the user home directory.
# This can be resolved granting group read access.
#
# chmod 0750 $HOME
#
user_run $HIST_USER zfs list ||
    log_unsupported "Test user $HIST_USER cannot execute zfs utilities"

run_and_verify "zfs create $root_testfs" "-l"
run_and_verify "zfs allow $HIST_GROUP snapshot,mount $root_testfs" "-l"
run_and_verify "zfs allow $HIST_USER destroy,mount $root_testfs" "-l"
run_and_verify "zfs allow $HIST_USER reservation $root_testfs" "-l"
run_and_verify "zfs allow $HIST_USER allow $root_testfs" "-l"
run_and_verify -u "$HIST_USER" "zfs snapshot $root_testfs@snap" "-l"
run_and_verify -u "$HIST_USER" "zfs destroy $root_testfs@snap" "-l"
run_and_verify -u "$HIST_USER" "zfs set reservation=64M $root_testfs" "-l"
run_and_verify -u "$HIST_USER" \
    "zfs allow $HIST_USER reservation $root_testfs" "-l"
run_and_verify "zfs unallow $HIST_USER create $root_testfs" "-l"
run_and_verify "zfs unallow $HIST_GROUP snapshot $root_testfs" "-l"
run_and_verify "zfs destroy -r $root_testfs" "-l"

log_pass "Verify internal long history information pass."
