#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
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
# Copyright (c) 2024 Klara, Inc.
#

# Check legacy dedup table continues to work after pool upgrade to fast_dedup,
# but if deleted and recreated, the new table is FDT

. $STF_SUITE/include/libtest.shlib

log_assert "legacy dedup tables work after upgrade; new dedup tables created as FDT"

# we set the dedup log txg interval to 1, to get a log flush every txg,
# effectively disabling the log. without this it's hard to predict when and
# where things appear on-disk
log_must save_tunable DEDUP_LOG_TXG_MAX
log_must set_tunable32 DEDUP_LOG_TXG_MAX 1

function cleanup
{
	destroy_pool $TESTPOOL
	log_must restore_tunable DEDUP_LOG_TXG_MAX
}

log_onexit cleanup

# create a pool with legacy dedup enabled. we disable compression so our writes
# create predictable results on disk
# Use 'xattr=sa' to prevent selinux xattrs influencing our accounting
log_must zpool create -f \
    -o feature@fast_dedup=disabled \
    -O dedup=on \
    -O compression=off \
    -O xattr=sa \
    $TESTPOOL $DISKS

# confirm the feature is disabled
log_must test $(get_pool_prop feature@fast_dedup $TESTPOOL) = "disabled"

# confirm there's no DDT keys in the MOS root
log_mustnot eval "zdb -dddd $TESTPOOL 1 | grep -q DDT-sha256"

# create a file. this is four full blocks, so will produce four entries in the
# dedup table
log_must dd if=/dev/urandom of=/$TESTPOOL/file1 bs=128k count=4
log_must zpool sync

# feature should still be disabled
log_must test $(get_pool_prop feature@fast_dedup $TESTPOOL) = "disabled"

# should be four entries in the unique table
log_must eval "zdb -D $TESTPOOL | grep -q 'DDT-sha256-zap-unique:.*entries=4'"

# should be just one DDT ZAP in the MOS
log_must test $(zdb -dddd $TESTPOOL 1 | grep DDT-sha256-zap- | wc -l) -eq 1

# enable the fast_dedup feature
log_must zpool set feature@fast_dedup=enabled $TESTPOOL

# confirm the feature is now enabled
log_must test $(get_pool_prop feature@fast_dedup $TESTPOOL) = "enabled"

# copy the file
log_must dd if=/$TESTPOOL/file1 of=/$TESTPOOL/file2 bs=128k
log_must zpool sync

# feature should still be enabled
log_must test $(get_pool_prop feature@fast_dedup $TESTPOOL) = "enabled"

# now four entries in the duplicate table
log_must eval "zdb -D $TESTPOOL | grep -q 'DDT-sha256-zap-duplicate:.*entries=4'"

# now two DDT ZAPs in the MOS; DDT ZAPs aren't cleaned up until the entire
# logical table is destroyed
log_must test $(zdb -dddd $TESTPOOL 1 | grep DDT-sha256-zap- | wc -l) -eq 2

# remove the files
log_must rm -f /$TESTPOOL/file*
log_must zpool sync

# feature should still be enabled
log_must test $(get_pool_prop feature@fast_dedup $TESTPOOL) = "enabled"

# all DDTs empty
log_must eval "zdb -D $TESTPOOL | grep -q 'All DDTs are empty'"

# logical table now destroyed; all DDT ZAPs removed
log_must test $(zdb -dddd $TESTPOOL 1 | grep DDT-sha256-zap- | wc -l) -eq 0

# create a new file
log_must dd if=/dev/urandom of=/$TESTPOOL/file3 bs=128k count=4
log_must zpool sync

# feature should now be active
log_must test $(get_pool_prop feature@fast_dedup $TESTPOOL) = "active"

# four entries in the unique table
log_must eval "zdb -D $TESTPOOL | grep -q 'DDT-sha256-zap-unique:.*entries=4'"

# single containing object in the MOS
log_must test $(zdb -dddd $TESTPOOL 1 | grep DDT-sha256 | wc -l) -eq 1
obj=$(zdb -dddd $TESTPOOL 1 | grep DDT-sha256 | awk '{ print $NF }')

# with one ZAP inside
log_must test $(zdb -dddd $TESTPOOL $obj | grep DDT-sha256-zap- | wc -l) -eq 1

log_must zdb -b $TESTPOOL

log_pass "legacy dedup tables work after upgrade; new dedup tables created as FDT"
