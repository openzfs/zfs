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
