#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
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

log_assert "legacy and FDT dedup tables on the same pool can happily coexist"

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

# create a pool with legacy dedup enabled. we disable block cloning to ensure
# it doesn't get in the way of dedup, and we disable compression so our writes
# create predictable results on disk
# Use 'xattr=sa' to prevent selinux xattrs influencing our accounting
log_must zpool create -f \
    -o feature@fast_dedup=disabled \
    -o feature@block_cloning=disabled \
    -O compression=off \
    -O xattr=sa \
    $TESTPOOL $DISKS

# create two datasets, enabling a different dedup algorithm on each
log_must zfs create -o dedup=skein $TESTPOOL/ds1
log_must zfs create -o dedup=blake3 $TESTPOOL/ds2

# confirm the feature is disabled
log_must test $(get_pool_prop feature@fast_dedup $TESTPOOL) = "disabled"

# confirm there's no DDT keys in the MOS root
log_mustnot eval "zdb -dddd $TESTPOOL 1 | grep -q DDT-skein"
log_mustnot eval "zdb -dddd $TESTPOOL 1 | grep -q DDT-blake3"

# create a file in the first dataset
log_must dd if=/dev/urandom of=/$TESTPOOL/ds1/file1 bs=128k count=4
log_must zpool sync

# should be four entries in the skein unique table
log_must eval "zdb -D $TESTPOOL | grep -q 'DDT-skein-zap-unique:.*entries=4'"

# should be just one DDT ZAP in the MOS
log_must test $(zdb -dddd $TESTPOOL 1 | grep DDT-.*-zap- | wc -l) -eq 1

# enable the fast_dedup feature
log_must zpool set feature@fast_dedup=enabled $TESTPOOL

# confirm the feature is now enabled
log_must test $(get_pool_prop feature@fast_dedup $TESTPOOL) = "enabled"

# create a file in the first dataset
log_must dd if=/dev/urandom of=/$TESTPOOL/ds2/file1 bs=128k count=4
log_must zpool sync

# feature should now be active
log_must test $(get_pool_prop feature@fast_dedup $TESTPOOL) = "active"

# now also four entries in the blake3 unique table
log_must eval "zdb -D $TESTPOOL | grep -q 'DDT-blake3-zap-unique:.*entries=4'"

# two entries in the MOS: the legacy skein DDT ZAP, and the containing dir for
# the blake3 FDT table
log_must test $(zdb -dddd $TESTPOOL 1 | grep DDT-.*-zap- | wc -l) -eq 1
log_must test $(zdb -dddd $TESTPOOL 1 | grep DDT-blake3 | wc -l) -eq 1

# containing object has one ZAP inside
obj=$(zdb -dddd $TESTPOOL 1 | grep DDT-blake3 | awk '{ print $NF }')
log_must test $(zdb -dddd $TESTPOOL $obj | grep DDT-.*-zap- | wc -l) -eq 1

log_pass "legacy and FDT dedup tables on the same pool can happily coexist"
