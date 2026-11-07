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
# Copyright (c) 2026 Tigerblue77
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	"zpool set feature@<name>=disabled" is refused while a property of a
#	dataset, set locally or received, has a value that needs the feature,
#	or while a volume has blocks larger than 128K, although nothing has
#	activated the feature yet.  While the feature is disabled, such a
#	value cannot be set.
#
# STRATEGY:
#	1. Create a pool on which large_blocks, large_dnode, sha512 and skein
#	   are enabled but not active, and disable large_microzap, which
#	   depends on large_blocks.
#	2. For recordsize=1M, dnodesize=1k, checksum=sha512 and dedup=skein,
#	   set the value on a file system that writes nothing: disabling the
#	   feature it needs must fail.  Once the property is inherited, the
#	   feature can be disabled, and setting the value must then fail.
#	3. Receive a file system with a received recordsize=1M and set
#	   recordsize=128k on it: disabling large_blocks must still fail,
#	   until the file system is destroyed.
#	4. Create a volume with volblocksize=1M and write nothing: disabling
#	   large_blocks must fail until the volume is destroyed.
#

verify_runnable "global"

VDEV=$TEST_BASE_DIR/feature_disable_props

function cleanup
{
	poolexists $TESTPOOL1 && destroy_pool $TESTPOOL1
	rm -f $VDEV
}

function check_prop # feature value
{
	typeset val=$(get_pool_prop feature@$1 $TESTPOOL1)

	[[ "$val" == "$2" ]] ||
	    log_fail "feature@$1 is '$val', expected '$2'"
}

log_assert "'zpool set feature@<name>=disabled' is refused while a" \
    "property value needs the feature"
log_onexit cleanup

log_must truncate -s $MINVDEVSIZE $VDEV
log_must zpool create -f $TESTPOOL1 $VDEV
log_must zpool set feature@large_microzap=disabled $TESTPOOL1

# 2. Local values.
for spec in large_blocks:recordsize=1M large_dnode:dnodesize=1k \
    sha512:checksum=sha512 skein:dedup=skein; do
	feature=${spec%%:*}
	propval=${spec#*:}
	prop=${propval%%=*}

	log_must zfs create $TESTPOOL1/fs
	sync_pool $TESTPOOL1
	log_must zfs set $propval $TESTPOOL1/fs
	check_prop $feature enabled
	log_mustnot zpool set feature@$feature=disabled $TESTPOOL1
	check_prop $feature enabled
	log_must zfs inherit $prop $TESTPOOL1/fs
	log_must zpool set feature@$feature=disabled $TESTPOOL1
	check_prop $feature disabled
	log_mustnot zfs set $propval $TESTPOOL1/fs
	log_must zpool set feature@$feature=enabled $TESTPOOL1
	log_must zfs destroy $TESTPOOL1/fs
done

# 3. A received value that a local one hides.
log_must zfs create -o recordsize=1M $TESTPOOL1/src
log_must zfs snapshot $TESTPOOL1/src@a
log_must eval "zfs send -p $TESTPOOL1/src@a | zfs receive $TESTPOOL1/dst"
log_must zfs destroy -r $TESTPOOL1/src
log_must zfs set recordsize=128k $TESTPOOL1/dst
log_must test "$(zfs get -H -o received recordsize $TESTPOOL1/dst)" == "1M"
check_prop large_blocks enabled
log_mustnot zpool set feature@large_blocks=disabled $TESTPOOL1
log_must zfs destroy -r $TESTPOOL1/dst
log_must zpool set feature@large_blocks=disabled $TESTPOOL1
log_must zpool set feature@large_blocks=enabled $TESTPOOL1

# 4. A volume with 1M blocks.
log_must zfs create -V 16M -b 1M $TESTPOOL1/vol
block_device_wait
check_prop large_blocks enabled
log_mustnot zpool set feature@large_blocks=disabled $TESTPOOL1
destroy_dataset $TESTPOOL1/vol
log_must zpool set feature@large_blocks=disabled $TESTPOOL1
check_prop large_blocks disabled

log_pass "'zpool set feature@<name>=disabled' is refused while a" \
    "property value needs the feature"
