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
# Copyright (c) 2012, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zfs_destroy/zfs_destroy.cfg
. $STF_SUITE/tests/functional/cli_root/zfs_destroy/zfs_destroy_common.kshlib

################################################################################
#
# 'zfs destroy -d <snap>' should mark snapshot for deferred destroy when
# clone exists and destroy when clone is destroyed.
#
# 1. Create test environment with clones.
# 2. 'zfs destroy -d <snap>'
# 3. Verify snapshot is marked for deferred destroy.
# 4. 'zfs destroy <clone>'
# 3. Verify that the snapshot and clone are destroyed.
#
################################################################################

function test_c_run
{
    typeset dstype=$1

    snap=$(eval echo \$${dstype}SNAP)
    clone=$(eval echo \$${dstype}CLONE)
    log_must zfs destroy -d $snap
    log_must datasetexists $snap
    log_must eval "[[ $(get_prop defer_destroy $snap) == 'on' ]]"
    log_must zfs destroy $clone
    log_mustnot datasetexists $snap
    log_mustnot datasetexists $clone
}

log_assert "'zfs destroy -d <snap>' marks cloned snapshot for deferred destroy"
log_onexit cleanup_testenv

setup_testenv clone

for dstype in FS VOL; do
    if [[ $dstype == VOL ]]; then
		if is_global_zone; then
			test_c_run $dstype
		fi
	else
		test_c_run $dstype
	fi
done

log_pass "'zfs destroy -d <snap>' marks cloned snapshot for deferred destroy"
