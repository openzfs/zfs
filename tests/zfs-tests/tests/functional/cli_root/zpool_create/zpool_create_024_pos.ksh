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
# Copyright (c) 2016, Lawrence Livermore National Security, LLC.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_create/zpool_create.cfg

#
# DESCRIPTION:
# Many 'zpool create' and 'zpool destroy' must succeed concurrently.
#
# STRATEGY:
# 1. Create N process each of which create/destroy a pool M times.
# 2. Allow all process to run to completion.
# 3. Verify all pools and their vdevs were destroyed.
#

verify_runnable "global"

if is_32bit; then
	log_unsupported "Test case runs slowly on 32 bit"
fi

function cleanup
{
	if [[ -n "$child_pids" ]]; then
		for wait_pid in $child_pids; do
			kill $wait_pid 2>/dev/null
		done
	fi

	if [[ -n "$child_pools" ]]; then
		for pool in $child_pools; do
			typeset vdev0="$TEST_BASE_DIR/$pool-vdev0.img"
			typeset vdev1="$TEST_BASE_DIR/$pool-vdev1.img"

			if poolexists $pool; then
				destroy_pool $pool
			fi

			rm -f $vdev0 $vdev1
		done
	fi
}

log_onexit cleanup

log_assert "Many 'zpool create' and 'zpool destroy' must succeed concurrently."

child_pids=""
child_pools=""

function zpool_stress
{
	typeset pool=$1
	typeset vdev0="$TEST_BASE_DIR/$pool-vdev0.img"
	typeset vdev1="$TEST_BASE_DIR/$pool-vdev1.img"
	typeset -i iters=$2
	typeset retry=10
	typeset j=0

	truncate -s $FILESIZE $vdev0
	truncate -s $FILESIZE $vdev1

	while [[ $j -lt $iters ]]; do
		((j = j + 1))
		sleep 1

		zpool create $pool $vdev0 $vdev1
		if [ $? -ne 0 ]; then
			return 1;
		fi

		# The 'zfs destroy' command is retried because it can
		# transiently return EBUSY when blkid is concurrently
		# probing new volumes and therefore has them open.
		typeset k=0;
		while [[ $k -lt $retry ]]; do
			((k = k + 1))

			zpool destroy $pool
			if [ $? -eq 0 ]; then
				break;
			elif [ $k -eq $retry ]; then
				return 1;
			fi

			sleep 3
		done
	done

	rm -f $vdev0 $vdev1
	return 0
}

# 1. Create 128 process each of which create/destroy a pool 5 times.
typeset i=0
while [[ $i -lt 128 ]]; do
	typeset uuid=$(uuidgen | cut -c1-13)

	zpool_stress $TESTPOOL-$uuid 5 &
	typeset pid=$!

	child_pids="$child_pids $pid"
	child_pools="$child_pools $TESTPOOL-$uuid"
	((i = i + 1))
done

# 2. Allow all process to run to completion.
wait

# 3. Verify all pools and their vdevs were destroyed.
for pool in $child_pools; do
	typeset vdev0="$TEST_BASE_DIR/$pool-vdev0.img"
	typeset vdev1="$TEST_BASE_DIR/$pool-vdev1.img"

	if poolexists $pool; then
		log_fail "pool $pool exists"
	fi

	if [ -e $vdev0 ]; then
		log_fail "pool vdev $vdev0 exists"
	fi

	if [ -e $vdev1 ]; then
		log_fail "pool vdev $vdev1 exists"
	fi
done

log_pass "Many 'zpool create' and 'zpool destroy' must succeed concurrently."
