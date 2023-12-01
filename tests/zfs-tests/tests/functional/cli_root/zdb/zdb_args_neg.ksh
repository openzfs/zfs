#!/bin/ksh -p
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or https://opensource.org/licenses/CDDL-1.0.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
#

#
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2012, 2017 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# A badly formed parameter passed to zdb(1) should
# return an error.
#
# STRATEGY:
# 1. Create an array containing bad zdb parameters.
# 2. For each element, execute the sub-command.
# 3. Verify it returns an error.
#

verify_runnable "global"

set -A args "create" "add" "destroy" "import fakepool" \
    "export fakepool" "create fakepool" "add fakepool" \
    "create mirror" "create raidz" \
    "create mirror fakepool" "create raidz fakepool" \
    "create raidz1 fakepool" "create raidz2 fakepool" \
    "create fakepool mirror" "create fakepool raidz" \
    "create fakepool raidz1" "create fakepool raidz2" \
    "add fakepool mirror" "add fakepool raidz" \
    "add fakepool raidz1" "add fakepool raidz2" \
    "add mirror fakepool" "add raidz fakepool" \
    "add raidz1 fakepool" "add raidz2 fakepool" \
    "setvprop" "blah blah" "-%" "--?" "-*" "-=" \
    "-a" "-f" "-g" "-j" "-n" "-o" "-p" "-p /tmp" \
    "-t" "-w" "-z" "-E" "-H" "-I" "-J" \
    "-Q" "-R" "-W"

log_assert "Execute zdb using invalid parameters."

log_onexit cleanup

function cleanup
{
	default_cleanup_noexit
}

function test_imported_pool
{
	for i in ${args[@]}; do
		log_mustnot zdb $i $TESTPOOL
	done
}

default_mirror_setup_noexit $DISKS

test_imported_pool

log_pass "Badly formed zdb parameters fail as expected."
