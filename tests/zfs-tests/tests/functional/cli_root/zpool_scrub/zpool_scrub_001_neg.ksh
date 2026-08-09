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
# Copyright (c) 2025 Hewlett Packard Enterprise Development LP.
# Copyright (c) 2026 ConnectWise
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_scrub/zpool_scrub.cfg

#
# DESCRIPTION:
# A badly formed parameter passed to 'zpool scrub' should
# return an error.
#
# STRATEGY:
# 1. Create an array containing bad 'zpool scrub' parameters.
# 2. For each element, execute the sub-command.
# 3. Verify it returns an error.
# 4. Verify that the mutually exclusive cli args are actually
#    mutually exclusive.
#

verify_runnable "global"

typeset -a args=("-?" "blah blah" "-%" "--?" "-*" "-=" \
    "-b" "-c" "-d" "-f" "-g" "-h" "-i" "-j" "-k" "-l" \
    "-m" "-n" "-o" "-q" "-r" "-u" "-v" "-x" "-y" "-z" \
    "-A" "-B" "-D" "-E" "-F" "-G" "-H" "-I" "-J" "-K" "-L" \
    "-M" "-N" "-O" "-P" "-Q" "-R" "-S" "-T" "-U" "-V" "-W" "-X" "-Y" "-Z")


log_assert "Execute 'zpool scrub' using invalid parameters."

for arg in "${args[@]}"; do
	log_mustnot zpool scrub "$arg" "$TESTPOOL"
done

log_mustnot zpool scrub -p -s $TESTPOOL

log_mustnot zpool scrub -e -p $TESTPOOL
log_mustnot zpool scrub -e -s $TESTPOOL
log_mustnot zpool scrub -e -C $TESTPOOL
log_mustnot zpool scrub -e -t $TESTPOOL
log_mustnot zpool scrub -e -S "2000-01-01" $TESTPOOL
log_mustnot zpool scrub -e -E "2099-12-31" $TESTPOOL
log_mustnot zpool scrub -e -S "2000-01-01" -E "2099-12-31" $TESTPOOL

log_mustnot zpool scrub -p -C $TESTPOOL
log_mustnot zpool scrub -p -t $TESTPOOL
log_mustnot zpool scrub -p -S "2000-01-01" $TESTPOOL
log_mustnot zpool scrub -p -E "2099-12-31" $TESTPOOL
log_mustnot zpool scrub -p -S "2000-01-01" -E "2099-12-31" $TESTPOOL

log_mustnot zpool scrub -s -C $TESTPOOL
log_mustnot zpool scrub -s -t $TESTPOOL
log_mustnot zpool scrub -s -S "2000-01-01" $TESTPOOL
log_mustnot zpool scrub -s -E "2099-12-31" $TESTPOOL
log_mustnot zpool scrub -s -S "2000-01-01" -E "2099-12-31" $TESTPOOL

log_mustnot zpool scrub -C -S "2000-01-01" $TESTPOOL
log_mustnot zpool scrub -C -E "2099-12-31" $TESTPOOL
log_mustnot zpool scrub -C -S "2000-01-01" -E "2099-12-31" $TESTPOOL

log_pass "Badly formed 'zpool scrub' parameters fail as expected."
