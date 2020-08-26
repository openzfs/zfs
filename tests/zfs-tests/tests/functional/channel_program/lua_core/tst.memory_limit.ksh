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
#       Passing memory limit options to channel programs should work correctly.
#       Programs that exceed these limits should fail gracefully.


verify_runnable "global"

log_mustnot_checkerror_program "Memory limit exhausted" \
    -t 100000000 $TESTPOOL - <<-EOF
	a = {};
	i = 0;
	while true do
		i = i + 1
		a[i] = "Here is the " .. i .. "th entry of a"
	end;
	return a
EOF

log_assert "memory limit options work"
log_mustnot_checkerror_program "Memory limit exhausted" \
    -m 100000 -t 100000000 $TESTPOOL - <<-EOF
	a = {};
	i = 0;
	while true do
	  i = i + 1
	  a[i] = "Here is the " .. i .. "th entry of a"
	end;
	return a
EOF

log_must_program -m 100000 $TESTPOOL - <<-EOF
	s = "teststring"
	s = s .. s .. s .. s
	return s
EOF

log_assert "very small memory limits fail correctly"
log_mustnot_checkerror_program "Memory limit exhausted" -m 1 $TESTPOOL - <<-EOF
	s = "teststring"
	s = s .. s .. s .. s
	return s
EOF

# Set the memlimit, in case it is a non-default value
log_must set_tunable32 LUA_MAX_MEMLIMIT 100000000

log_mustnot_checkerror_program "Invalid instruction or memory limit" \
    -m 200000000 $TESTPOOL - <<-EOF
	return 1;
EOF

log_mustnot_checkerror_program "Return value too large" \
    -m 9223372036854775808 $TESTPOOL - <<-EOF
	return 1;
EOF

log_pass "Memory limits work correctly."
