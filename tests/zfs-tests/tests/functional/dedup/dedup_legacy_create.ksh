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

# Simple test of dedup table operations (legacy)

. $STF_SUITE/include/libtest.shlib

log_assert "basic dedup (legacy) operations work"

function cleanup
{
	destroy_pool $TESTPOOL
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

# copy the file
log_must dd if=/$TESTPOOL/file1 of=/$TESTPOOL/file2 bs=128k
log_must zpool sync

# now four entries in the duplicate table
log_must eval "zdb -D $TESTPOOL | grep -q 'DDT-sha256-zap-duplicate:.*entries=4'"

# now two DDT ZAPs in the MOS; DDT ZAPs aren't cleaned up until the entire
# logical table is destroyed
log_must test $(zdb -dddd $TESTPOOL 1 | grep DDT-sha256-zap- | wc -l) -eq 2

# remove the files
log_must rm -f /$TESTPOOL/file*
log_must zpool sync

# feature should still be disabled
log_must test $(get_pool_prop feature@fast_dedup $TESTPOOL) = "disabled"

# all DDTs empty
log_must eval "zdb -D $TESTPOOL | grep -q 'All DDTs are empty'"

# logical table now destroyed; all DDT ZAPs removed
log_must test $(zdb -dddd $TESTPOOL 1 | grep DDT-sha256-zap- | wc -l) -eq 0

log_must zdb -b $TESTPOOL

log_pass "basic dedup (legacy) operations work"
