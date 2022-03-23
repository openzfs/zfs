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
#       Overflowing a 64-bit integer should wrap around.
#

verify_runnable "global"

log_assert "overflowing a 64-bit integer should wrap around"

log_must_program $TESTPOOL - <<<"assert(18446744073709551615 + 1 == (-18446744073709551616))"

log_pass "overflowing a 64-bit integer should wrap around"
