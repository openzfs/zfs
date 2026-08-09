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
# Copyright (c) 2020 The FreeBSD Foundation [1]
#
# [1] Portions of this software were developed by Allan Jude
#     under sponsorship from the FreeBSD Foundation.

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Setting the compression property to any of the zstd levels should activate
# the zstd feature flag. Destroying the last dataset using the zstd feature flag
# should revert the feature to the 'enabled' state.
#
# STRATEGY:
# 1. Create pool, then create a file system within it.
# 2. Check that the zstd feature flag is 'enabled'.
# 3. Setting the compression property to zstd.
# 4. Check that the zstd feature flag is now 'active'.
# 5. Destroy the dataset
# 6. Confirm that the feature flag reverts to the 'enabled' state.
#

verify_runnable "both"

log_assert "Setting compression=zstd should activate the"\
	"org.freebsd:zstd_compress feature flag, and destroying the last"\
	"dataset using that property, should revert the feature flag to"\
	"the enabled state."

export VDEV_ZSTD="$TEST_BASE_DIR/vdev-zstd"

function cleanup
{
	if poolexists $TESTPOOL-zstd ; then
		destroy_pool $TESTPOOL-zstd
	fi

	rm $VDEV_ZSTD
}
log_onexit cleanup

log_must truncate -s $SPA_MINDEVSIZE $VDEV_ZSTD
log_must zpool create $TESTPOOL-zstd $VDEV_ZSTD

featureval="$(get_pool_prop feature@zstd_compress $TESTPOOL-zstd)"

[[ "$featureval" == "disabled" ]] && \
	log_unsupported "ZSTD feature flag unsupposed"

[[ "$featureval" == "active" ]] && \
	log_unsupported "ZSTD feature already active before test"

random_level=$((RANDOM%19 + 1))
log_note "Randomly selected ZSTD level: $random_level"

log_must zfs create -o compress=zstd-$random_level $TESTPOOL-zstd/$TESTFS-zstd

featureval="$(get_pool_prop feature@zstd_compress $TESTPOOL-zstd)"

log_note "After zfs set, feature flag value is: $featureval"

[[ "$featureval" == "active" ]] ||
	log_fail "ZSTD feature flag not activated"

log_must zfs destroy $TESTPOOL-zstd/$TESTFS-zstd

featureval="$(get_pool_prop feature@zstd_compress $TESTPOOL-zstd)"

log_note "After zfs destroy, feature flag value is: $featureval"

[[ "$featureval" == "enabled" ]] ||
	log_fail "ZSTD feature flag not deactivated"

log_pass "Setting compression=zstd activated the feature flag, and"\
	"destroying the dataset deactivated it."
