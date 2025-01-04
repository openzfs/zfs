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

log_must zpool export $TESTPOOL1 &

zpool set comment=hello $TESTPOOL1
zpool reguid $TESTPOOL1 &
zpool split $TESTPOOL1 &

log_pass "admin commands cannot race a pool export"
