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
