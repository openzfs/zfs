#!/bin/ksh -p
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or https://opensource.org/licenses/CDDL-1.0.
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
# Copyright (c) 2024 Klara, Inc.
#

# Check legacy dedup table continues to work after pool upgrade to fast_dedup,
# but if deleted and recreated, the new table is FDT

. $STF_SUITE/include/libtest.shlib

log_assert "legacy dedup tables work after upgrade; new dedup tables created as FDT"

function cleanup
{
	destroy_pool $TESTPOOL
}

log_onexit cleanup

# create a pool with legacy dedup enabled. we disable block cloning to ensure
# it doesn't get in the way of dedup, and we disable compression so our writes
# create predictable results on disk
log_must zpool create -f \
    -o feature@fast_dedup=disabled \
    -O dedup=on \
    -o feature@block_cloning=disabled \
    -O compression=off \
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
log_must eval "zdb -D $TESTPOOL | grep -q 'DDT-sha256-zap-unique: 4 entries'"

# should be just one DDT ZAP in the MOS
log_must test $(zdb -dddd $TESTPOOL 1 | grep DDT-sha256-zap- | wc -l) -eq 1

# enable the fast_dedup feature
log_must zpool set feature@fast_dedup=enabled $TESTPOOL

# confirm the feature is now enabled
log_must test $(get_pool_prop feature@fast_dedup $TESTPOOL) = "enabled"

# copy the file
log_must cp /$TESTPOOL/file1 /$TESTPOOL/file2
log_must zpool sync

# feature should still be enabled
log_must test $(get_pool_prop feature@fast_dedup $TESTPOOL) = "enabled"

# now four entries in the duplicate table
log_must eval "zdb -D $TESTPOOL | grep -q 'DDT-sha256-zap-duplicate: 4 entries'"

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
log_must eval "zdb -D $TESTPOOL | grep -q 'DDT-sha256-zap-unique: 4 entries'"

# single containing object in the MOS
log_must test $(zdb -dddd $TESTPOOL 1 | grep DDT-sha256 | wc -l) -eq 1
obj=$(zdb -dddd $TESTPOOL 1 | grep DDT-sha256 | awk '{ print $NF }')

# with one ZAP inside
log_must test $(zdb -dddd $TESTPOOL $obj | grep DDT-sha256-zap- | wc -l) -eq 1

log_pass "legacy dedup tables work after upgrade; new dedup tables created as FDT"
