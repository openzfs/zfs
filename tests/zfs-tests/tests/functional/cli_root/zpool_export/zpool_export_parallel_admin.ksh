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
# Copyright (c) 2024 Klara, Inc.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# 	Verify that admin commands cannot race a pool export
#
# STRATEGY:
#	1. Create a pool
#	2. Import the pool with an injected delay in the background
#	3. Execute some admin commands against the pool
#

verify_runnable "global"

DEVICE_DIR=$TEST_BASE_DIR/dev_export-test

function cleanup
{
	zinject -c all
	poolexists $TESTPOOL1 && destroy_pool $TESTPOOL1
	[[ -d $DEVICE_DIR ]] && log_must rm -rf $DEVICE_DIR
}

log_assert "admin commands cannot race a pool export"

log_onexit cleanup

[[ ! -d $DEVICE_DIR ]] && log_must mkdir -p $DEVICE_DIR
log_must truncate -s $MINVDEVSIZE ${DEVICE_DIR}/disk0 ${DEVICE_DIR}/disk1

log_must zpool create -f $TESTPOOL1 mirror ${DEVICE_DIR}/disk0 ${DEVICE_DIR}/disk1

log_must zinject -P export -s 10 $TESTPOOL1

log_must_busy zpool export $TESTPOOL1 &

zpool set comment=hello $TESTPOOL1
zpool reguid $TESTPOOL1 &
zpool split $TESTPOOL1 &

log_pass "admin commands cannot race a pool export"
