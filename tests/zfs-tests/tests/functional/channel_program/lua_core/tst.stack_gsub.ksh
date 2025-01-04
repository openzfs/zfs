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
# Copyright (c) 2020 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/channel_program/channel_common.kshlib

#
# DESCRIPTION:
#       Overflowing the C stack using recursive gsub() should be handled
#       gracefully.  gsub() uses more stack space than typical, so it relies
#       on LUAI_MINCSTACK to ensure that we don't overflow the Linux kernel's
#       stack.
#

verify_runnable "global"

log_assert "recursive gsub() should be handled gracefully"

log_mustnot_program $TESTPOOL $ZCP_ROOT/lua_core/tst.stack_gsub.zcp

log_pass "recursive gsub() should be handled gracefully"
