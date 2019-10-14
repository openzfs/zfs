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
# Copyright (C) 2022 Aleksa Sarai <cyphar@cyphar.com>
# Copyright (C) 2022 SUSE LLC
#

. $STF_SUITE/tests/functional/slog/slog.kshlib

#
# DESCRIPTION:
#   Verify that features activated after "zpool freeze" are not actually
#   activated. This is actually an unwanted feature of spa_freeze() and
#   resulted in certain workarounds for feature@rename_{exchange,whiteout} in
#   slog_replay_fs_001. The purpose of this test is to document the behaviour
#   so that if it is ever fixed we can remove the slog_replay_fs_001
#   workaround.
#
#   This test is based on slog_replay_fs_001.
#
# STRATEGY:
#	1. Create an empty file system (TESTFS)
#   2. Enable the rename_* features.
#	3. Freeze TESTFS
#   4. Trigger the activation of the rename_* features.
#   5. Verify that the features are not active but the ZIL contains new
#      TX_RENAME_* entries.
#
# Critically we must not cause the ZIL to be replayed because that will cause
# an assertion to fail (because the feature is not activated and there is an
# assertion verifying this on zfs_replay).
#

verify_runnable "global"

log_assert "Activation of features during spa_freeze silently fails."
log_onexit cleanup
log_must setup

if ! is_linux ; then
	log_unsupported "renameat2 is linux-only"
elif ! renameat2 -C ; then
	log_unsupported "renameat2 not supported on this (pre-3.15) linux kernel"
fi

#
# 1. Create an empty file system (TESTFS)
#
log_must zpool create $TESTPOOL $VDEV log mirror $LDEV
log_must zfs set compression=on $TESTPOOL
log_must zfs create $TESTPOOL/$TESTFS

#
# 2. Enable the rename_* features.
#
# RENAME_EXCHANGE
zfs set "feature@rename_exchange=enabled" "$TESTPOOL"
# RENAME_WHITEOUT
zfs set "feature@rename_whiteout=enabled" "$TESTPOOL"

# Make sure the features are *enabled*, not active.
zpool sync "$TESTPOOL"
check_feature_flag "feature@rename_exchange" "$TESTPOOL" "enabled"
check_feature_flag "feature@rename_whiteout" "$TESTPOOL" "enabled"

#
# This dd command works around an issue where ZIL records aren't created
# after freezing the pool unless a ZIL header already exists. Create a file
# synchronously to force ZFS to write one out.
#
log_must dd if=/dev/zero of=/$TESTPOOL/$TESTFS/sync \
    conv=fdatasync,fsync bs=1 count=1

#
# 3. Freeze TESTFS
#
log_must zpool freeze $TESTPOOL

#
# 4. Trigger the activation of the rename_* features.
#

# TX_RENAME_EXCHANGE
log_must dd if=/dev/urandom of=/$TESTPOOL/$TESTFS/xchg-a bs=1k count=1
log_must dd if=/dev/urandom of=/$TESTPOOL/$TESTFS/xchg-b bs=1k count=1
log_must dd if=/dev/urandom of=/$TESTPOOL/$TESTFS/xchg-c bs=1k count=1
log_must dd if=/dev/urandom of=/$TESTPOOL/$TESTFS/xchg-d bs=1k count=1
# rotate the files around
log_must renameat2 -x /$TESTPOOL/$TESTFS/xchg-{a,b}
log_must renameat2 -x /$TESTPOOL/$TESTFS/xchg-{b,c}
log_must renameat2 -x /$TESTPOOL/$TESTFS/xchg-{c,a}
# exchange same path
log_must renameat2 -x /$TESTPOOL/$TESTFS/xchg-{d,d}

# TX_RENAME_WHITEOUT
log_must mkfile 1k /$TESTPOOL/$TESTFS/whiteout
log_must renameat2 -w /$TESTPOOL/$TESTFS/whiteout{,-moved}

#
# 5. Verify that the features are not active but the ZIL contains new
#    TX_RENAME_* entries.
#
check_feature_flag "feature@rename_exchange" "$TESTPOOL" "enabled"
check_feature_flag "feature@rename_whiteout" "$TESTPOOL" "enabled"
log_must eval "zdb -iv $TESTPOOL/$TESTFS | grep 'TX_RENAME_EXCHANGE'"
log_must eval "zdb -iv $TESTPOOL/$TESTFS | grep 'TX_RENAME_WHITEOUT'"

log_pass "Activation of features during spa_freeze silently fails."
