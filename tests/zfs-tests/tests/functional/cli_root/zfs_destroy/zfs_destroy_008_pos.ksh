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
# 'zfs destroy -d <snap>' should destroy the snapshot when no clone exists.
#
# 1. Create test environment without clones.
# 2. 'zfs destroy -d <snap>'
# 3. Verify that the snapshot was destroyed.
#
################################################################################

function test_s_run
{
    typeset snap=$1

    log_must zfs destroy -d $snap
    log_mustnot datasetexists $snap	
}

log_assert "'zfs destroy -d <snap>' destroys snapshot if there is no clone"
log_onexit cleanup_testenv

setup_testenv snap

for snap in $FSSNAP $VOLSNAP; do
    if [[ $snap == $VOLSNAP ]]; then
		if is_global_zone; then
			test_s_run $snap
		fi
	else
		test_s_run $snap
	fi
done

log_pass "'zfs destroy -d <snap>' destroys snapshot if there is no clone"
