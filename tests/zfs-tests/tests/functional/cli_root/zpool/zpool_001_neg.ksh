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
# A badly formed sub-command passed to zpool(1) should
# return an error.
#
# STRATEGY:
# 1. Create an array containing each zpool sub-command name.
# 2. For each element, execute the sub-command.
# 3. Verify it returns an error.
#

verify_runnable "both"


set -A args "" "create" "add" "destroy" "import fakepool" \
    "export fakepool" "create fakepool" "add fakepool" \
    "create mirror" "create raidz" "create raidz1" \
    "create mirror fakepool" "create raidz fakepool" \
    "create raidz1 fakepool" "create raidz2 fakepool" \
    "create fakepool mirror" "create fakepool raidz" \
    "create fakepool raidz1" "create fakepool raidz2" \
    "add fakepool mirror" "add fakepool raidz" \
    "add fakepool raidz1" "add fakepool raidz2" \
    "add mirror fakepool" "add raidz fakepool" \
    "add raidz1 fakepool" "add raidz2 fakepool" \
    "setvprop" "blah blah" "-%" "--" "--?" "-*" "-="

log_assert "Execute zpool sub-command without proper parameters."

typeset -i i=0
while [[ $i -lt ${#args[*]} ]]; do
	log_mustnot zpool ${args[i]}

	((i = i + 1))
done

log_pass "Badly formed zpool sub-commands fail as expected."
