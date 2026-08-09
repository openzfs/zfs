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
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#
# Zpool get usage message is displayed when called with no arguments
#
# STRATEGY:
#	1. Run zpool get
#	2. Check that exit status is set to 2
#	3. Check usage message contains text "usage"
#

log_assert "Zpool get usage message is displayed when called with no arguments."

zpool get > /dev/null 2>&1
RET=$?
if [ $RET != 2 ]
then
	log_fail "\"zpool get\" exit status $RET should be equal to 2."
fi

OUTPUT=$(zpool get 2>&1 | grep -i usage)
RET=$?
if [ $RET != 0 ]
then
	log_fail "Usage message for zpool get did not contain the word 'usage'."
fi

log_pass "Zpool get usage message is displayed when called with no arguments."
