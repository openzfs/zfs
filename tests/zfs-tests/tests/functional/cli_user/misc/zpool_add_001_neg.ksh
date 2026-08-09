#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0

# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# https://opensource.org/license/CDDL-1.0.
#

#
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_user/misc/misc.cfg

#
# DESCRIPTION:
# Verify that 'zpool add' fails as non-root.
#
# STRATEGY:
# 1. Create an array of options.
# 2. Execute each element of the array.
# 3. Verify that an error is returned.
#

verify_runnable "global"

ADD_DISK="${DISKS%% }"
ADD_DISK="${ADD_DISK##* }"

[[ -z $ADD_DISK ]] && \
        log_fail "No spare disks available."

# Under Linux dry-run commands have no legitimate reason to fail.
if is_linux; then
	set -A args "add" "add -f" "add -n" \
	    "add $TESTPOOL" "add -f $TESTPOOL" "add -n $TESTPOOL" \
	    "add -fn $TESTPOOL" "add -nf $TESTPOOL" \
	    "add $TESTPOOL $ADD_DISK" "add -f $TESTPOOL $ADD_DISK"
else
	set -A args "add" "add -f" "add -n" \
	    "add $TESTPOOL" "add -f $TESTPOOL" "add -n $TESTPOOL" \
	    "add -fn $TESTPOOL" "add -nf $TESTPOOL" \
	    "add $TESTPOOL $ADD_DISK" "add -f $TESTPOOL $ADD_DISK" \
	    "add -n $TESTPOOL $ADD_DISK" \
	    "add -fn $TESTPOOL $ADD_DISK" \
	    "add -nf $TESTPOOL $ADD_DISK"
fi

log_assert "zpool add [-fn] pool_name vdev"

typeset -i i=0
while [[ $i -lt ${#args[*]} ]]; do
	log_mustnot zpool ${args[i]}
	((i = i + 1))
done

log_pass "The sub-command 'add' and its options fail as non-root."
