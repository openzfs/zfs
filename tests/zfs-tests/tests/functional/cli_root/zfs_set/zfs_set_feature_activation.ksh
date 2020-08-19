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
