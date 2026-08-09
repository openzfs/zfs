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

#
# DESCRIPTION:
# Creating a volume with a 50 letter name should work.
#
# STRATEGY:
# 1. Using a very long name, create a zvol
# 2. Verify volume exists
#

verify_runnable "global"

function cleanup
{
	datasetexists $TESTPOOL/$LONGVOLNAME && \
		destroy_dataset $TESTPOOL/$LONGVOLNAME
}

log_onexit cleanup

log_assert "Creating a volume a 50 letter name should work."

LONGVOLNAME="volumename50charslong_0123456789012345678901234567"

log_must zfs create -V $VOLSIZE $TESTPOOL/$LONGVOLNAME

datasetexists $TESTPOOL/$LONGVOLNAME || \
	log_fail "Couldn't find long volume name"

log_pass "Created a 50-letter zvol volume name"
