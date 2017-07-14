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
# Copyright (c) 2017 Datto Inc.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# A badly formed parameter passed to 'zpool sync' should
# return an error.
#
# STRATEGY:
# 1. Create an array containing bad 'zpool sync' parameters.
# 2. For each element, execute the sub-command.
# 3. Verify it returns an error.
#

verify_runnable "global"

set -A args "1" "-a" "-?" "--%" "-123456" "0.5" "-o" "-b" "-b no" "-z 2"

log_assert "Execute 'zpool sync' using invalid parameters."

typeset -i i=0
while [[ $i -lt ${#args[*]} ]]; do
	log_mustnot zpool sync ${args[i]}
	((i = i + 1))
done

log_pass "Invalid parameters to 'zpool sync' fail as expected."
