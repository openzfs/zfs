#!/bin/ksh -p
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
