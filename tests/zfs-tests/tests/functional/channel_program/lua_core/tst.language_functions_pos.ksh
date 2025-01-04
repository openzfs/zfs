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

#
# DESCRIPTION:
#	Try very simple programs that interact with the core lua
#       runtime rather than ZFS functions, just to make sure the
#       runtime is hooked up correctly.
#

verify_runnable "global"

set -A args "" \
	"assert(true)" \
	"x = 1 + 1"

log_assert "Simple lua scripts pass."

typeset -i i=0
while (( i < ${#args[*]} )); do
	log_must_program $TESTPOOL - <<<"${args[i]}"
	((i = i + 1))
done

log_pass "Simple lua scripts pass."
