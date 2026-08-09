#! /bin/ksh -p
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
# Copyright (c) 2023 by Lawrence Livermore National Security, LLC.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#
# https://github.com/openzfs/zfs/issues/15526 identified a dirty dnode
# SEEK_HOLE/SEEK_DATA bug.  https://github.com/openzfs/zfs/pull/15571
# fixed the bug, and was backported to 2.1.14 and 2.2.2.
#
# This test is to ensure that the bug, as understood, will not recur.
#
# STRATEGY:
#
# 1. Run the 'seekflood' binary, for creation of files with timing
#    characteristics that can trigger #15526.
# 2. A single run is not always a trigger, so run repeatedly.

verify_runnable "global"

function cleanup
{
	rm -rf /$TESTDIR/cp_stress
}

log_assert "Run the 'seekflood' binary repeatedly to try to trigger #15526"

log_onexit cleanup

log_must mkdir /$TESTPOOL/cp_stress

MYPWD="$PWD"
cd /$TESTPOOL/cp_stress
CPUS=$(get_num_cpus)

# should run in ~2 minutes on Linux and FreeBSD
RUNS=3
for i in $(seq 1 $RUNS) ; do
	# Each run takes around 12 seconds.
	log_must $STF_SUITE/tests/functional/cp_files/seekflood 2000 $CPUS
done
cd "$MYPWD"

log_pass "No corruption detected"
