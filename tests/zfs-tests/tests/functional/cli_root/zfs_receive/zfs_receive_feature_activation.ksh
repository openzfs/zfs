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
#	A stream whose feature flags say that the sender has large_dnode or
#	zstd_compress active activates the feature on the received file
#	system, even when the stream holds no large dnode or zstd block.
#
# STRATEGY:
#	1. Create a source and a destination pool, with large_dnode and
#	   zstd_compress enabled but not active.
#	2. Activate large_dnode on a source file system with a file whose
#	   dnode is 1K, remove the file, and snapshot it.
#	3. Activate zstd_compress on a second source file system by setting
#	   compression=zstd, and snapshot it.
#	4. Receive full and incremental streams of both: the feature must be
#	   active in the destination pool, and enabled again once the
#	   received file systems are destroyed.  An uncompressed stream,
#	   which has no zstd flag, must not activate zstd_compress.
#

verify_runnable "both"

log_assert "A received stream activates the large_dnode and zstd_compress" \
	"features that its feature flags name."

SRC=$TESTPOOL-fsrc
DST=$TESTPOOL-fdst
VDEV_SRC="$TEST_BASE_DIR/vdev-fsrc"
VDEV_DST="$TEST_BASE_DIR/vdev-fdst"

function cleanup
{
	poolexists $SRC && destroy_pool $SRC
	poolexists $DST && destroy_pool $DST
	rm -f $VDEV_SRC $VDEV_DST
}
log_onexit cleanup

function check_feature # pool feature expected-value step
{
	typeset value

	value="$(get_pool_prop feature@$2 $1)"
	[[ "$value" == "$3" ]] || log_fail "$4: $1 feature@$2 is $value," \
	    "expected $3"
	log_note "$4: $1 feature@$2 is $value"
}

log_must truncate -s $SPA_MINDEVSIZE $VDEV_SRC $VDEV_DST
log_must zpool create $SRC $VDEV_SRC
log_must zpool create $DST $VDEV_DST

for feature in large_dnode zstd_compress; do
	case "$(get_pool_prop feature@$feature $DST)" in
	disabled)
		log_unsupported "$feature feature not supported" ;;
	active)
		log_unsupported "$feature feature already active" ;;
	esac
done

# 2. large_dnode active on $SRC/ld@b, which holds no large dnode
log_must zfs create $SRC/ld
log_must zfs snapshot $SRC/ld@a
log_must zfs set dnodesize=1k $SRC/ld
mntpnt=$(get_prop mountpoint $SRC/ld)
log_must touch $mntpnt/file
log_must zpool sync $SRC
check_feature $SRC large_dnode active "1K dnode written"
log_must rm $mntpnt/file
log_must zpool sync $SRC
log_must zfs snapshot $SRC/ld@b

# 3. zstd_compress active on $SRC/z@b, which holds no zstd block
log_must zfs create $SRC/z
log_must zfs snapshot $SRC/z@a
log_must zfs set compression=zstd $SRC/z
log_must zfs snapshot $SRC/z@b
check_feature $SRC zstd_compress active "compression=zstd set"

# 4. large_dnode
log_must eval "zfs send $SRC/ld@b | zfs receive $DST/ld"
check_feature $DST large_dnode active "full receive"
log_must zfs destroy -r $DST/ld
check_feature $DST large_dnode enabled "zfs destroy"

log_must eval "zfs send $SRC/ld@a | zfs receive $DST/ld"
check_feature $DST large_dnode enabled "receive of a stream without the flag"
log_must eval "zfs send -i @a $SRC/ld@b | zfs receive -F $DST/ld"
check_feature $DST large_dnode active "incremental receive"
log_must zfs destroy -r $DST/ld
check_feature $DST large_dnode enabled "zfs destroy"

# 4. zstd_compress
log_must eval "zfs send $SRC/z@b | zfs receive $DST/z"
check_feature $DST zstd_compress enabled "uncompressed full receive"
log_must zfs destroy -r $DST/z

log_must eval "zfs send -c $SRC/z@b | zfs receive $DST/z"
log_mustnot test "$(get_prop compression $DST/z)" == "zstd"
check_feature $DST zstd_compress active "compressed full receive"
log_must zfs destroy -r $DST/z
check_feature $DST zstd_compress enabled "zfs destroy"

log_must eval "zfs send -c $SRC/z@a | zfs receive $DST/z"
check_feature $DST zstd_compress enabled "receive of a stream without the flag"
log_must eval "zfs send -c -i @a $SRC/z@b | zfs receive -F $DST/z"
check_feature $DST zstd_compress active "compressed incremental receive"
log_must zfs destroy -r $DST/z
check_feature $DST zstd_compress enabled "zfs destroy"

log_pass "A received stream activates the large_dnode and zstd_compress" \
	"features that its feature flags name."
