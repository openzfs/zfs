#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
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
# Copyright (c) 2018 by Datto. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# A badly formed parameter passed to 'zpool resilver' should
# return an error.
#
# STRATEGY:
# 1. Create an array containing bad 'zpool reilver' parameters.
# 2. For each element, execute the sub-command.
# 3. Verify it returns an error.
# 4. Confirm the sub-command returns an error if the resilver_defer
#    feature isn't active.
#

verify_runnable "global"

set -A args "" "-?" "blah blah" "-%" "--?" "-*" "-=" \
    "-a" "-b" "-c" "-d" "-e" "-f" "-g" "-h" "-i" "-j" "-k" "-l" \
    "-m" "-n" "-o" "-p" "-q" "-r" "-s" "-t" "-u" "-v" "-w" "-x" "-y" "-z" \
    "-A" "-B" "-C" "-D" "-E" "-F" "-G" "-H" "-I" "-J" "-K" "-L" \
    "-M" "-N" "-O" "-P" "-Q" "-R" "-S" "-T" "-U" "-V" "-W" "-X" "-W" "-Z"

function cleanup
{
	log_must destroy_pool $TESTPOOL2
	log_must rm -f $TEST_BASE_DIR/zpool_resilver.dat
}

log_onexit cleanup

log_assert "Execute 'zpool resilver' using invalid parameters."

typeset -i i=0
while [[ $i -lt ${#args[*]} ]]; do
	log_mustnot zpool resilver ${args[i]}

	((i = i + 1))
done

log_must mkfile $MINVDEVSIZE $TEST_BASE_DIR/zpool_resilver.dat
log_must zpool create -d $TESTPOOL2 $TEST_BASE_DIR/zpool_resilver.dat
log_mustnot zpool resilver $TESTPOOL2

log_pass "Badly formed 'zpool resilver' parameters fail as expected."
