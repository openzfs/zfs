#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
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
# Copyright (c) 2019 by Tim Chase. All rights reserved.
# Copyright (c) 2019 Lawrence Livermore National Security, LLC.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	A badly formed parameter passed to 'zpool condense' should
#	return an error.
#
# STRATEGY:
#	1. Create an array containing bad 'zpool condense' parameters.
#	2. For each element, execute the sub-command.
#	3. Verify it returns an error.
#

verify_runnable "global"

typeset -a args=( \
    "-?" "-n" "-1" "-xz1" "--yoyo" \
    "-t" "-t none" "--type" "--type none" \
    "-type" "-type none" "-type log_spacemap" "-all" "-cancel" "-wait")

log_assert "Execute 'zpool condense' using invalid parameters."

typeset -i i=0
while [[ $i -lt ${#args[*]} ]]; do
	log_mustnot zpool condense ${args[i]} $TESTPOOL
	((i = i + 1))
done

log_pass "Invalid parameters to 'zpool condense' fail as expected."
