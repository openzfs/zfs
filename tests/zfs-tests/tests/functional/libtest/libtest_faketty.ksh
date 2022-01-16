#!/bin/ksh -p
#
# CDDL HEADER START
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
# CDDL HEADER END
#

#
# Copyright (c) 2020 Adam Moss, Inc. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# libtest's faketty should conform to expected semantics
#
# STRATEGY:
# 1. Verify that faketty as subordinate works and correctly
#      returns error code of sub-subordinates
# 2. Verify that faketty without subordinates is pass-through
# 3. Verify the faketty return code when used directly as function
#

verify_runnable "both"

function cleanup
{
	# nothing special to do
	true
}
log_onexit cleanup

log_assert "libtest's faketty conforms to expected semantics"

# NOTE: this is a tough set of tests for faketty, more strict than
# expected by current users of faketty - could relax

# 1.
# `which tty` is weird, it's not finding the 'tty' util otherwise
log_must eval "echo foo | faketty `which tty`"
log_mustnot eval "echo foo | `which tty`"
log_mustnot eval "echo foo | faketty false"
log_mustnot eval "echo foo | faketty true"

# 2.
log_must test foo = $(echo foo | cat)
# (how common is 'echo -n' support?)
log_must test foo = $(echo -n foo | faketty)

# 3.
log_mustnot faketty "echo foo | false"
log_must faketty "echo foo | true"
log_must faketty "`which tty`"
log_mustnot "`which tty`"
log_must faketty true
log_mustnot faketty false

log_pass "libtest's faketty conforms to expected semantics"


