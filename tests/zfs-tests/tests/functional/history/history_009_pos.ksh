#!/bin/ksh -p
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
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
