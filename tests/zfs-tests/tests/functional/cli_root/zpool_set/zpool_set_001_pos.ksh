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
# Copyright (c) 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
#
# DESCRIPTION:
#
# Zpool set usage message is displayed when called with no arguments
#
# STRATEGY:
#	1. Run zpool set
#	2. Check that exit status is set to 2
#	3. Check usage message contains text "usage"
#
#

log_assert "zpool set usage message is displayed when called with no arguments"

zpool set > /dev/null 2>&1
RET=$?
if [ $RET != 2 ]
then
	log_fail "\"zpool set\" exit status $RET should be equal to 2."
fi

zpool set 2>&1 | grep -qi usage
if [ $? != 0 ]
then
	log_fail "Usage message for zpool set did not contain the word 'usage'."
fi

log_pass "zpool set usage message is displayed when called with no arguments"
