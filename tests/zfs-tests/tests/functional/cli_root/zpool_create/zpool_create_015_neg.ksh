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
# Copyright (c) 2012 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_create/zpool_create.shlib
. $TMPFILE

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
		if [[ -n "$LINUX" ]]; then
			swap_dev=/dev/$(get_physical_device $ZVOL_DEVDIR/${vol_name})
			swap_opt="-s"
		else
			swap_dev=$ZVOL_DEVDIR/$vol_name
			swap_opt="-l"
		fi

		$SWAP $swap_opt | $GREP $swap_dev > /dev/null 2>&1
		if [[ $? -eq 0 ]]; then
			if [[ -n "$LINUX" ]]; then
				swapoff $swap_dev
			else
				$SWAP -d $ZVOL_DEVDIR/${vol_name}
			fi
		fi
	fi

	for pool in $TESTPOOL1 $TESTPOOL; do
		destroy_pool -f $pool
	done

}

if [[ -n $DISK ]]; then
        disk=$DISK
else
        disk=$DISK0
fi

typeset slice_part=s
[[ -n "$LINUX" ]] && slice_part=p

typeset pool_dev=${disk}${slice_part}${SLICE0}
typeset vol_name=$TESTPOOL/$TESTVOL

log_assert "'zpool create' should fail with zfs vol device in swap."
log_onexit cleanup

#
# use zfs vol device in swap to create pool which should fail.
#
create_pool $TESTPOOL $pool_dev
log_must $ZFS create -V 100m $vol_name
if [[ -n "$LINUX" ]]; then
	sleep 1
	log_must mkswap $ZVOL_DEVDIR/$vol_name
	log_must $SWAP $ZVOL_DEVDIR/$vol_name
else
	log_must $SWAP -a $ZVOL_DEVDIR/$vol_name
fi
for opt in "-n" "" "-f"; do
	log_mustnot $ZPOOL create $opt $TESTPOOL1 $ZVOL_DEVDIR/${vol_name}
done

# cleanup
if [[ -n "$LINUX" ]]; then
	swap_dev=/dev/$(get_physical_device $ZVOL_DEVDIR/${vol_name})
	log_must swapoff $swap_dev
else
	log_must $SWAP -d $ZVOL_DEVDIR/${vol_name}
fi

destroy_dataset $vol_name
destroy_pool $TESTPOOL

log_pass "'zpool create' passed as expected with inapplicable scenario."
