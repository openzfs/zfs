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
#       Constructing integers that are doubles, too large, or too
#       small should fail gracefully.
#

verify_runnable "global"

log_assert "constructing illegal integer values should fail gracefully"

set -A args "1.0" \
	"1.5" \
	"-1.5"

typeset -i i=0
while (( i < ${#args[*]} )); do
	log_mustnot_checkerror_program "malformed number" $TESTPOOL - <<-EOF
		return ${args[i]}
	EOF
	((i = i + 1))
done

log_pass "constructing illegal integer values should fail gracefully"
