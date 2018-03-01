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
#	Try returning various lua values that should be converted
#       to nvlists. Also, try to pass them to error().
#

verify_runnable "global"

set -A args "" \
	"nil" \
	"true" \
	"1" \
	"\"test\"" \
	"{}" \
	"{val=0}" \
	"{{1, {2, 3}, {val1={val2=true}}}, {test=\"yes\"}}" \
	"EINVAL"

log_assert "Returning valid lua constructs works."

typeset -i i=0
while (( i < ${#args[*]} )); do
	log_note "running program: return ${args[i]}"
	log_must_program $TESTPOOL - <<-EOF
		return ${args[i]}
	EOF
	((i = i + 1))
done

typeset -i i=0
while (( i < ${#args[*]} )); do
	log_note "running program: error(${args[i]})"
	log_mustnot_checkerror_program "in function 'error'" $TESTPOOL - <<-EOF
		error(${args[i]})
	EOF
	((i = i + 1))
done

log_pass "Returning valid lua constructs works."
