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
# https://opensource.org/license/CDDL-1.0.
#

#
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Executing 'zpool status' command with bad options fails.
#
# STRATEGY:
# 1. Create an array of badly formed 'zpool status' options
# 2. Execute each element of the array.
# 3. Verify an error code is returned.
#

verify_runnable "both"


set -A args "" "-?" "-x fakepool" "-v fakepool" "-xv fakepool" "-vx fakepool" \
	"-x $TESTPOOL/$TESTFS" "-v $TESTPOOL/$TESTFS" "-xv $TESTPOOL/$TESTFS" \
	"-vx $TESTPOOL/$TESTFS"

log_assert "Executing 'zpool status' with bad options fails"

typeset -i i=1

while [[ $i -lt ${#args[*]} ]]; do

	log_mustnot zpool status ${args[$i]}

	(( i = i + 1 ))
done

log_pass "'zpool status' command with bad options failed as expected."
