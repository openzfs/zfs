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
# http://www.illumos.org/license/CDDL.
#

#
# Copyright (c) 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/channel_program/channel_common.kshlib

verify_runnable "global"

arch=$(uname -m)

if [[ "$arch" == "sparc64" ]]; then
	log_unsupported "May cause stack overflow on sparc64 due to recursion"
else
	log_mustnot_checkerror_program "too many C levels" \
	    $TESTPOOL $ZCP_ROOT/lua_core/tst.nested_neg.zcp

	log_pass "Too many nested lua statements fail cleanly."
fi
