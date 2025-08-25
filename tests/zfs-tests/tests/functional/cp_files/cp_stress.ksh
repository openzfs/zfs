#! /bin/ksh -p
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
