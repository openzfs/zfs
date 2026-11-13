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
#	Setting compression=zstd activates the zstd_compress feature on the
#	file system, and the feature must stay active while that file system
#	exists, even when it is rolled back or receives a stream onto a
#	snapshot taken before the property was set.
#
# STRATEGY:
#	1. Create a pool on which zstd_compress is enabled but not active.
#	2. Snapshot a file system, set compression=zstd, and roll it back:
#	   the feature must still be active.  Destroying the file system
#	   returns it to enabled.
#	3. Receive an incremental stream onto an existing file system with
#	   "-o compression=zstd": the feature must be active.
#	4. Receive an incremental stream onto an existing file system with
#	   a received compression=zstd, then destroy the sending file system:
#	   the feature must still be active.
#

verify_runnable "both"

log_assert "A file system set to compression=zstd keeps zstd_compress" \
	"active across rollback and receive."

POOL=$TESTPOOL-zstd
VDEV_ZSTD="$TEST_BASE_DIR/vdev-zstd"

function cleanup
{
	poolexists $POOL && destroy_pool $POOL
	rm -f $VDEV_ZSTD
}
log_onexit cleanup

function check_zstd_feature # expected-value step
{
	typeset value

	value="$(get_pool_prop feature@zstd_compress $POOL)"
	[[ "$value" == "$1" ]] || log_fail "$2: feature@zstd_compress" \
	    "is $value, expected $1"
	log_note "$2: feature@zstd_compress is $value"
}

log_must truncate -s $SPA_MINDEVSIZE $VDEV_ZSTD
log_must zpool create $POOL $VDEV_ZSTD

case "$(get_pool_prop feature@zstd_compress $POOL)" in
disabled)
	log_unsupported "zstd_compress feature not supported" ;;
active)
	log_unsupported "zstd_compress feature already active" ;;
esac

# 2. zfs rollback
log_must zfs create $POOL/fs
log_must zfs snapshot $POOL/fs@a
log_must zfs set compression=zstd $POOL/fs
check_zstd_feature active "zfs set"
log_must zfs rollback $POOL/fs@a
check_zstd_feature active "zfs rollback"
log_must zfs destroy -r $POOL/fs
check_zstd_feature enabled "zfs destroy"

# 3. zfs receive -o compression=zstd
log_must zfs create $POOL/src
log_must zfs snapshot $POOL/src@a
log_must eval "zfs send $POOL/src@a | zfs receive $POOL/dst"
log_must zfs snapshot $POOL/src@b
log_must eval "zfs send -i @a $POOL/src@b |" \
    "zfs receive -F -o compression=zstd $POOL/dst"
log_must test "$(get_prop compression $POOL/dst)" == "zstd"
check_zstd_feature active "zfs receive -o"
log_must zfs destroy -r $POOL/dst
check_zstd_feature enabled "zfs destroy"

# 4. received compression=zstd
log_must eval "zfs send $POOL/src@b | zfs receive $POOL/dst"
log_must zfs set compression=zstd $POOL/src
log_must zfs snapshot $POOL/src@c
log_must eval "zfs send -p -i @b $POOL/src@c | zfs receive -F $POOL/dst"
log_must test "$(get_prop compression $POOL/dst)" == "zstd"
log_must zfs destroy -r $POOL/src
check_zstd_feature active "received compression"
log_must zfs destroy -r $POOL/dst
check_zstd_feature enabled "zfs destroy"

log_pass "A file system set to compression=zstd keeps zstd_compress" \
	"active across rollback and receive."
