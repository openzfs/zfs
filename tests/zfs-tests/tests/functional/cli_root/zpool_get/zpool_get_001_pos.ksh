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
