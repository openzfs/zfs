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
# Executing 'zpool status' with correct options succeeds
#
# STRATEGY:
# 1. Create an array of correctly formed 'zpool status' options
# 2. Execute each element of the array.
# 3. Verify use of each option is successful.
#

verify_runnable "both"

typeset testpool
if is_global_zone; then
	testpool=$TESTPOOL
else
	testpool=${TESTPOOL%%/*}
fi

set -A args "" "-x" "-v" "-x $testpool" "-v $testpool" "-xv $testpool" \
	"-vx $testpool" "-e $testpool" "-es $testpool"

log_assert "Executing 'zpool status' with correct options succeeds"

typeset -i i=0

while [[ $i -lt ${#args[*]} ]]; do

	log_must zpool status ${args[$i]}

	(( i = i + 1 ))
done

cleanup 

log_pass "'zpool status' with correct options succeeded"
