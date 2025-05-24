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
# http://www.illumos.org/license/CDDL.
#

. $STF_SUITE/tests/functional/alloc_class/alloc_class.kshlib

#
# DESCRIPTION:
#	File blocks and zvol blocks, where special_small_blocks is active,
#	are expected to end up in the special class.
#

verify_runnable "global"

claim="File and zvol blocks using special_small_blocks end up in special class"

log_assert $claim
log_onexit cleanup
log_must disk_setup

log_must zpool create $TESTPOOL $ZPOOL_DISKS special $CLASS_DISK0

# Provision a filesystem with special_small_blocks and copy 10M to it
log_must zfs create -o compression=off -o special_small_blocks=32K \
	-o recordsize=32K $TESTPOOL/$TESTFS
log_must dd if=/dev/urandom of=/$TESTPOOL/$TESTFS/testfile bs=1M count=10

# Provision a volume with special_small_blocks and copy 10M to it
log_must zfs create -V 100M -b 32K -o special_small_blocks=32K \
	-o compression=off $TESTPOOL/$TESTVOL
block_device_wait "$ZVOL_DEVDIR/$TESTPOOL/$TESTVOL"
log_must dd if=/dev/urandom of=$ZVOL_DEVDIR/$TESTPOOL/$TESTVOL bs=1M count=10

sync_pool $TESTPOOL
zpool list -v $TESTPOOL

# Get the amount allocated to special vdev using vdev 'allocated' property
result=$(zpool get -Hp allocated $TESTPOOL $CLASS_DISK0)
set -- $result
allocated=$3
echo $allocated bytes allocated on special device $CLASS_DISK0

# Confirm that at least 20M was allocated
if [[ $allocated -lt 20971520 ]]
then
	log_fail "$allocated on special vdev $CLASS_DISK0, but expecting 20M"
fi

log_must zpool destroy -f "$TESTPOOL"
log_pass $claim
