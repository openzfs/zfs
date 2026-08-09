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
