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
# Copyright (c) 2017 Open-E, Inc. All Rights Reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Wrong arguments passed to zpool reopen should cause an error.
#
# STRATEGY:
# 1. Create an array with bad 'zpool reopen' arguments.
# 2. For each argument execute the 'zpool reopen' command and verify
#    if it returns an error.
#

verify_runnable "global"

# 1. Create an array with bad 'zpool reopen' arguments.
typeset -a args=("!" "1" "-s" "--n" "-1" "-" "-c" "-f" "-d 2" "-abc" "-na")

log_assert "Test 'zpool reopen' with invalid arguments."

# 2. For each argument execute the 'zpool reopen' command and verify
#    if it returns an error.
for arg in ${args[@]}; do
	log_mustnot zpool reopen $arg
done

log_pass "Passing invalid arguments to 'zpool reopen' failed as expected."
