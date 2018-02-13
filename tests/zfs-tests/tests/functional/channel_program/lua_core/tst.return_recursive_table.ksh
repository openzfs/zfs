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

verify_runnable "global"

. $STF_SUITE/tests/functional/channel_program/channel_common.kshlib

#
# DESCRIPTION:
#       Returning a recursive table should fail gracefully
#

verify_runnable "both"

log_assert "Returning a recursive table should fail gracefully."

log_mustnot_checkerror_program "Maximum table depth" \
    $TESTPOOL $ZCP_ROOT/lua_core/tst.return_recursive_table.zcp

log_pass "Returning a recursive table should fail gracefully."
