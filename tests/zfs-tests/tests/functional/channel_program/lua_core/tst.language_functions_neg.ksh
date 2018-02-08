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
#	Try channel programs with various lua runtime issues.
#       the program should fail, but the system should not crash.
#       Issues include:
#       * syntax errors
#       * misuse of language constructs (e.g. indexing non-tables)
#       * the error() function
#       * the assert() function
#

verify_runnable "global"

set -A args "{]" \
	"retrn 1" \
	"abc = nil; abc.deref" \
        "abc = nil; abc()" \
	"error(0)" \
	"error(\"string\")" \
	"error(true)" \
	"error({})" \
	"assert(false)"

log_assert "Runtime errors in lua scripts fail as expected."

typeset -i i=0
while (( i < ${#args[*]} )); do
	log_mustnot_checkerror_program "execution failed" $TESTPOOL - <<-EOF
		${args[i]}
	EOF
	((i = i + 1))
done

log_pass "Runtime errors in lua scripts fail as expected."
