#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# http://www.illumos.org/license/CDDL.
#

#
# Copyright 2015 Nexenta Systems, Inc.  All rights reserved.
#

. $STF_SUITE/tests/functional/casenorm/casenorm.kshlib

# DESCRIPTION:
# Check that we can create FS with any supported casesensitivity value.
#
# STRATEGY:
# For all supported casesensitivity values:
# 1. Create FS with given casesensitivity value.

verify_runnable "global"

function cleanup
{
	destroy_testfs
}

log_onexit cleanup
log_assert "Can create FS with any supported casesensitivity value"

for caseval in sensitive insensitive mixed; do
	create_testfs "-o casesensitivity=$caseval"
	destroy_testfs
done

log_pass "Can create FS with any supported casesensitivity value"
