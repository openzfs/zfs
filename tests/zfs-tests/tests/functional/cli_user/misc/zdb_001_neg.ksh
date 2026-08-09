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
	log_mustnot eval "$* | grep -q 'Dataset mos'"
}


function cleanup
{
	rm -f $TEST_BASE_DIR/zdb_001_neg.$$.txt
}

verify_runnable "global"

log_assert "zdb can't run as a user on datasets, but can run without arguments"
log_onexit cleanup

log_must eval "zdb > $TEST_BASE_DIR/zdb_001_neg.$$.txt"
# verify the output looks okay
log_must grep -q pool_guid $TEST_BASE_DIR/zdb_001_neg.$$.txt
log_must rm $TEST_BASE_DIR/zdb_001_neg.$$.txt

# we shouldn't able to run it on any dataset
check_zdb zdb $TESTPOOL
check_zdb zdb $TESTPOOL/$TESTFS
check_zdb zdb $TESTPOOL/$TESTFS@snap
check_zdb zdb $TESTPOOL/$TESTFS.clone

log_pass "zdb can't run as a user on datasets, but can run without arguments"
