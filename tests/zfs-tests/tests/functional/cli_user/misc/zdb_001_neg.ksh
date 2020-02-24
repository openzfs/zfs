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
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_user/misc/misc.cfg

#
# DESCRIPTION:
#
# zdb can't run as a user on datasets, but can run without arguments
#
# STRATEGY:
# 1. Run zdb as a user, it should print information
# 2. Run zdb as a user on different datasets, it should fail
#

function check_zdb
{
	$@ > $TEST_BASE_DIR/zdb.$$
	grep "Dataset mos" $TEST_BASE_DIR/zdb.$$
	if [ $? -eq 0 ]
	then
		log_fail "$@ exited 0 when run as a non root user!"
	fi
	rm $TEST_BASE_DIR/zdb.$$
}


function cleanup
{
	rm -f $TEST_BASE_DIR/zdb_001_neg.$$.txt $TEST_BASE_DIR/zdb.$$
}

verify_runnable "global"

log_assert "zdb can't run as a user on datasets, but can run without arguments"
log_onexit cleanup

log_must eval "zdb > $TEST_BASE_DIR/zdb_001_neg.$$.txt"
# verify the output looks okay
log_must grep pool_guid $TEST_BASE_DIR/zdb_001_neg.$$.txt
log_must rm $TEST_BASE_DIR/zdb_001_neg.$$.txt

# we shouldn't able to run it on any dataset
check_zdb zdb $TESTPOOL
check_zdb zdb $TESTPOOL/$TESTFS
check_zdb zdb $TESTPOOL/$TESTFS@snap
check_zdb zdb $TESTPOOL/$TESTFS.clone

log_pass "zdb can't run as a user on datasets, but can run without arguments"
