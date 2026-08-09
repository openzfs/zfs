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
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Try each ZFS volume sub-command without parameters to make sure
# it returns an error.
#
# STRATEGY:
# 1. Create an array of parameters
# 2. For each parameter in the array, execute the sub-command
# 3. Verify an error is returned.
#

verify_runnable "global"

set -A args "" "create -V" "create -V $TESTPOOL" \
	"create -V $TESTPOOL/$TESTVOL@" "create -V blah" "destroy"

log_assert "Try each ZFS volume sub-command without parameters to make sure" \
    " it returns an error."

typeset -i i=0
while (( $i < ${#args[*]} )); do
	log_mustnot zfs ${args[i]}
	(( i = i + 1 ))
done

log_pass "Badly formed ZFS volume sub-commands fail as expected."
