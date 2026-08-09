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
. $STF_SUITE/tests/functional/cli_root/zpool_create/zpool_create.shlib

################################################################################
#
#  When using the '-d' option with '-o feature@XXX=enabled' only the specified
#  feature is enabled.
#
#  1. Create a new pool with '-d' and '-o feature@async_destroy=enabled'.
#     async_destroy does not depend on anything so it should be the only
#     feature that gets enabled.
#  2. Verify that every feature@ property except feature@async_destroy is in
#     the 'disabled' state
#
################################################################################

verify_runnable "global"

function cleanup
{
	datasetexists $TESTPOOL && log_must zpool destroy $TESTPOOL
}

log_onexit cleanup

log_assert "'zpool create -d -o feature@async_destroy=enabled' only " \
    "enables async_destroy"

log_must zpool create -f -d -o feature@async_destroy=enabled $TESTPOOL $DISKS

state=$(zpool list -Ho feature@async_destroy $TESTPOOL)
if [[ "$state" != "enabled" ]]; then
	log_fail "async_destroy has state $state"
fi

for prop in $(zpool get all $TESTPOOL | awk '$2 ~ /feature@/ { print $2 }'); do
	state=$(zpool list -Ho "$prop" $TESTPOOL)
	if [[ "$prop" != "feature@async_destroy" \
	    && "$state" != "disabled" ]]; then
		log_fail "$prop is enabled on new pool"
        fi
done

log_pass
