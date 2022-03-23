#! /bin/ksh -p
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
# Copyright (c) 2018 by Lawrence Livermore National Security, LLC.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_expand/zpool_expand.cfg

#
# DESCRIPTION:
# Once zpool set autoexpand=on poolname, zpool can autoexpand by
# Dynamic VDEV Expansion
#
#
# STRATEGY:
# 1) Create three vdevs (loopback, scsi_debug, and file)
# 2) Create pool by using the different devices and set autoexpand=on
# 3) Expand each device as appropriate
# 4) Check that the pool size was expanded
#
# NOTE: Three different device types are used in this test to verify
# expansion of non-partitioned block devices (loopback), partitioned
# block devices (scsi_debug), and non-disk file vdevs.  ZFS volumes
# are not used in order to avoid a possible lock inversion when
# layering pools on zvols.
#

verify_runnable "global"

function cleanup
{
	poolexists $TESTPOOL1 && destroy_pool $TESTPOOL1

	if losetup -a | grep -q $DEV1; then
		losetup -d $DEV1
	fi

	rm -f $FILE_LO $FILE_RAW

	block_device_wait
	unload_scsi_debug
}

log_onexit cleanup

log_assert "zpool can be autoexpanded after set autoexpand=on on vdev expansion"

for type in " " mirror raidz draid:1s; do
	log_note "Setting up loopback, scsi_debug, and file vdevs"
	log_must truncate -s $org_size $FILE_LO
	DEV1=$(losetup -f)
	log_must losetup $DEV1 $FILE_LO

	load_scsi_debug $org_size_mb 1 1 1 '512b'
	block_device_wait
	DEV2=$(get_debug_device)

	log_must truncate -s $org_size $FILE_RAW
	DEV3=$FILE_RAW

	# The -f is required since we're mixing disk and file vdevs.
	log_must zpool create -f -o autoexpand=on $TESTPOOL1 $type \
	    $DEV1 $DEV2 $DEV3

	typeset autoexp=$(get_pool_prop autoexpand $TESTPOOL1)
	if [[ $autoexp != "on" ]]; then
		log_fail "zpool $TESTPOOL1 autoexpand should be on but is " \
		    "$autoexp"
	fi

	typeset prev_size=$(get_pool_prop size $TESTPOOL1)
	typeset zfs_prev_size=$(get_prop avail $TESTPOOL1)

	# Expand each device as appropriate being careful to add an artificial
	# delay to ensure we get a single history entry for each.  This makes
	# is easier to verify each expansion for the striped pool case, since
	# they will not be merged in to a single larger expansion.
	log_note "Expanding loopback, scsi_debug, and file vdevs"
	log_must truncate -s $exp_size $FILE_LO
	log_must losetup -c $DEV1
	sleep 3

	echo "2" > /sys/bus/pseudo/drivers/scsi_debug/virtual_gb
	echo "1" > /sys/class/block/$DEV2/device/rescan
	block_device_wait
	sleep 3

	log_must truncate -s $exp_size $FILE_RAW
	log_must zpool online -e $TESTPOOL1 $FILE_RAW

	typeset expand_size=$(get_pool_prop size $TESTPOOL1)
	typeset zfs_expand_size=$(get_prop avail $TESTPOOL1)

	log_note "$TESTPOOL1 $type has previous size: $prev_size and " \
	    "expanded size: $expand_size"
	# compare available pool size from zfs
	if [[ $zfs_expand_size -gt $zfs_prev_size ]]; then
		# check for zpool history for the pool size expansion
		if [[ $type == " " ]]; then
			typeset expansion_size=$(($exp_size-$org_size))
			typeset	size_addition=$(zpool history -il $TESTPOOL1 |\
			    grep "pool '$TESTPOOL1' size:" | \
			    grep "vdev online" | \
			    grep -c "(+${expansion_size}")

			if [[ $size_addition -ne 3 ]]; then
				log_fail "pool $TESTPOOL1 has not expanded, " \
				    "$size_addition/3 vdevs expanded"
			fi
		elif [[ $type == "mirror" ]]; then
			typeset expansion_size=$(($exp_size-$org_size))
			zpool history -il $TESTPOOL1 | \
			    grep "pool '$TESTPOOL1' size:" | \
			    grep "vdev online" | \
			    grep -q "(+${expansion_size})" ||
				log_fail "pool $TESTPOOL1 has not expanded"
		elif [[ $type == "draid:1s" ]]; then
			typeset expansion_size=$((2*($exp_size-$org_size)))
			zpool history -il $TESTPOOL1 | \
			    grep "pool '$TESTPOOL1' size:" | \
			    grep "vdev online" | \
			    grep -q "(+${expansion_size})" ||
					log_fail "pool $TESTPOOL has not expanded"
		else
			typeset expansion_size=$((3*($exp_size-$org_size)))
			zpool history -il $TESTPOOL1 | \
			    grep "pool '$TESTPOOL1' size:" | \
			    grep "vdev online" | \
			    grep -q "(+${expansion_size})" ||
					log_fail "pool $TESTPOOL has not expanded"
		fi
	else
		log_fail "pool $TESTPOOL1 is not autoexpanded after vdev " \
		    "expansion.  Previous size: $zfs_prev_size and expanded " \
		    "size: $zfs_expand_size"
	fi

	cleanup
done

log_pass "zpool can autoexpand if autoexpand=on after vdev expansion"
