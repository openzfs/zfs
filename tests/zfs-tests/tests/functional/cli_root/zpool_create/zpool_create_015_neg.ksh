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
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2012, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_create/zpool_create.shlib

#
#
# DESCRIPTION:
# 'zpool create' will fail with zfs vol device in swap
#
#
# STRATEGY:
# 1. Create a zpool
# 2. Create a zfs vol on zpool
# 3. Add this zfs vol device to swap
# 4. Try to create a new pool with devices in swap
# 5. Verify the creation is failed.
#

verify_runnable "global"

function cleanup
{
	# cleanup zfs pool and dataset
	if datasetexists $vol_name; then
		swap_cleanup ${ZVOL_DEVDIR}/${vol_name}
	fi

	for pool in $TESTPOOL1 $TESTPOOL; do
		poolexists $pool && destroy_pool $pool
	done
}

unset NOINUSE_CHECK
typeset vol_name=$TESTPOOL/$TESTVOL

log_assert "'zpool create' should fail with zfs vol device in swap."
log_onexit cleanup

#
# use zfs vol device in swap to create pool which should fail.
#
create_pool $TESTPOOL $DISK0
log_must zfs create -V 100m $vol_name
block_device_wait
swap_setup ${ZVOL_DEVDIR}/$vol_name

if is_freebsd; then
	typeset -a opts=("" "-f")
else
	typeset -a opts=("-n" "" "-f")
fi
for opt in "${opts[@]}"; do
	log_mustnot zpool create $opt $TESTPOOL1 ${ZVOL_DEVDIR}/${vol_name}
done

# cleanup
swap_cleanup ${ZVOL_DEVDIR}/${vol_name}
log_must_busy zfs destroy $vol_name
log_must zpool destroy $TESTPOOL

log_pass "'zpool create' passed as expected with inapplicable scenario."
