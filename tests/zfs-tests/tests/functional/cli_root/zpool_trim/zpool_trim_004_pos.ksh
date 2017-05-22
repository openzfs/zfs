#!/bin/ksh -p
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
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
# Copyright (c) 2017 Lawrence Livermore National Security, LLC.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/trim/trim.cfg
. $STF_SUITE/tests/functional/trim/trim.kshlib

#
# DESCRIPTION:
#	Verify 'zpool trim -p' partial trim.
#
# STRATEGY:
#	1. Create a pool on the provided VDEVS to TRIM.
#	2. Run 'zpool trim -p' to only TRIM allocated space maps.
#	3. Verify the vdevs are at least 90% of their original size.
#	4. Run 'zpool trim' to perform a full TRIM.
#	5. Verify the vdevs are less than 10% of their original size.

verify_runnable "global"

log_assert "Run 'zpool trim -p' to perform a partial TRIM"
log_onexit cleanup_trim

log_must mkfile $VDEV_SIZE $VDEVS
log_must zpool create -o cachefile=none -f $TRIMPOOL raidz $VDEVS

typeset vdev_min_size=$(( floor(VDEV_SIZE * 0.10 / 1024 / 1024) ))
typeset vdev_max_size=$(( floor(VDEV_SIZE * 0.90 / 1024 / 1024) ))

do_trim $TRIMPOOL "-p"
check_vdevs "-gt" "$vdev_max_size"

do_trim $TRIMPOOL
check_vdevs "-lt" "$vdev_min_size"

log_must zpool destroy $TRIMPOOL

log_pass "Manual 'zpool trim -p' successfully TRIMmed pool"
