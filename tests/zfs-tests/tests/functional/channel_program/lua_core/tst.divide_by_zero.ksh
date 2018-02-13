#!/bin/ksh -p
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

#
# DESCRIPTION:
#       Divide by zero should fail gracefully.
#

verify_runnable "global"

log_assert "Divide by zero should fail gracefully."

log_mustnot_program $TESTPOOL $ZCP_ROOT/lua_core/tst.divide_by_zero.zcp

log_pass "Divide by zero should fail gracefully."
