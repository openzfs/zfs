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
#	Verify the delegation internal history are correctly.
#
# STRATEGY:
#	1. Create test group and user.
#	2. Define permission sets and verify the internal history correctly.
#	3. Separately verify the internal history above is correct.
#

verify_runnable "global"

function cleanup
{
	del_user $HIST_USER
	del_group $HIST_GROUP
}

log_assert "Verify delegated commands are logged in the pool history."
log_onexit cleanup

testfs=$TESTPOOL/$TESTFS
# Create history test group and user and get user id and group id
add_group $HIST_GROUP
add_user $HIST_GROUP $HIST_USER

#	subcmd		allow_options
array=(	"allow"		"-s @basic snapshot"
	"allow"		"-s @set @basic"
	"allow"		"-c create"
	"unallow"	"-c create"
	"allow"		"-c @set"
	"unallow"	"-c @set"
	"allow"		"-l -u $HIST_USER snapshot"
	"allow"		"-u $HIST_USER snapshot"
	"unallow"	"-u $HIST_USER snapshot"
	"allow"		"-l -u $HIST_USER @set"
	"allow"		"-u $HIST_USER @set"
	"unallow"	"-u $HIST_USER @set"
	"allow"		"-d -u $HIST_USER snapshot"
	"allow"		"-u $HIST_USER snapshot"
	"unallow"	"-u $HIST_USER snapshot"
	"allow"		"-d -u $HIST_USER @set"
	"allow"		"-u $HIST_USER @set"
	"unallow"	"-u $HIST_USER @set"
	"allow"		"-l -g $HIST_GROUP snapshot"
	"allow"		"-g $HIST_GROUP snapshot"
	"unallow"	"-g $HIST_GROUP snapshot"
	"allow"		"-l -g $HIST_GROUP @set"
	"allow"		"-g $HIST_GROUP @set"
	"unallow"	"-g $HIST_GROUP @set"
	"allow"		"-d -g $HIST_GROUP snapshot"
	"allow"		"-g $HIST_GROUP snapshot"
	"unallow"	"-g $HIST_GROUP snapshot"
	"allow"		"-d -g $HIST_GROUP @set"
	"allow"		"-g $HIST_GROUP @set"
	"unallow"	"-g $HIST_GROUP @set"
	"allow"		"-l -e snapshot"
	"allow"		"-e snapshot"
	"unallow"	"-e snapshot"
	"allow"		"-l -e @set"
	"allow"		"-e @set"
	"unallow"	"-e @set"
	"allow"		"-d -e snapshot"
	"allow"		"-e snapshot"
	"unallow"	"-e snapshot"
	"allow"		"-d -e @set"
	"allow"		"-e @set"
	"unallow"	"-e @set"
)

typeset -i i=0
while ((i < ${#array[@]})); do
	subcmd=${array[$i]}
	options=${array[((i + 1))]}

	run_and_verify "zfs $subcmd $options $testfs" "-i"
	((i += 2))
done

log_pass "Verify delegated commands are logged in the pool history."
