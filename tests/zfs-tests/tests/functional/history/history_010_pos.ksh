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
