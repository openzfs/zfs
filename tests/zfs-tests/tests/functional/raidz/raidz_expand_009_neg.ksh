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

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	A RAIDZ expansion uses the boot area reserved on each disk as scratch
#	space, and that area has to hold one sector for each disk of the
#	expanded vdev.  'zpool attach' should refuse an expansion that does
#	not fit and leave the vdev as it was, but accept the widest one that
#	does.
#
# STRATEGY:
#	1. Create a raidz1 pool with ashift=16 and as many disks as the boot
#	   area holds sectors (56)
#	2. Verify that attaching one more disk fails and that the vdev still
#	   has 56 disks
#	3. Create a raidz1 pool with ashift=16 and 55 disks
#	4. Verify that attaching one more disk succeeds, that the expansion
#	   completes, and that the pool has no errors
#

typeset -r ashift=16
# VDEV_BOOT_SIZE (3.5M) divided by the sector size
typeset -r maxdevs=$(( (7 << 19) >> ashift ))
typeset -r dev_size_mb=128

typeset -a disks
typeset -i ndisks

function cleanup
{
	poolexists "$TESTPOOL" && log_must_busy zpool destroy "$TESTPOOL"

	for i in {1..$((maxdevs + 1))}; do
		log_must rm -f "$TEST_BASE_DIR/dev-$i"
	done
}

function count_disks # pool
{
	zpool status -P "$1" | grep -c "$TEST_BASE_DIR/dev-"
}

log_onexit cleanup

for i in {1..$((maxdevs + 1))}; do
	disks[$i]=$TEST_BASE_DIR/dev-$i
	log_must truncate -s ${dev_size_mb}M ${disks[$i]}
done

opts="-o cachefile=none -o ashift=$ashift"

# The expanded vdev would need one sector more than the boot area holds.
log_must zpool create -f $opts $TESTPOOL raidz1 ${disks[1..$maxdevs]}
log_mustnot_expect "too wide to be expanded" \
    zpool attach -f $TESTPOOL raidz1-0 ${disks[$((maxdevs + 1))]}
ndisks=$(count_disks $TESTPOOL)
if [[ $ndisks -ne $maxdevs ]]; then
	log_fail "raidz1-0 has $ndisks disks, expected $maxdevs"
fi
log_must zpool destroy $TESTPOOL

# The expanded vdev fills the boot area exactly.
log_must zpool create -f $opts $TESTPOOL raidz1 ${disks[1..$((maxdevs - 1))]}
log_must zpool attach -f -w $TESTPOOL raidz1-0 ${disks[$maxdevs]}
ndisks=$(count_disks $TESTPOOL)
if [[ $ndisks -ne $maxdevs ]]; then
	log_fail "raidz1-0 has $ndisks disks, expected $maxdevs"
fi
is_pool_scrubbing $TESTPOOL && wait_scrubbed $TESTPOOL
verify_pool $TESTPOOL
log_must zpool destroy $TESTPOOL

log_pass "raidz expansion is refused only when the boot area is too small"
