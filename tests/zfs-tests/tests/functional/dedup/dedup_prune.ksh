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
# Copyright (c) 2025, Klara Inc.
#

# DESCRIPTION:
#	Verify that zpool ddtprune successfully reduces the number of entries
#	in the DDT.
#
# STRATEGY:
#	1. Create a pool with dedup=on
#	2. Add duplicate entries to the DDT
#	3. Verify ddtprune doesn't remove duplicate entries
#	4. Convert some entries to non-duplicate
#	5. Verify ddtprune removes non-duplicate entries
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/events/events_common.kshlib

verify_runnable "both"

log_assert "Verify DDT pruning correctly removes non-duplicate entries"

# We set the dedup log txg interval to 1, to get a log flush every txg,
# effectively disabling the log. Without this it's hard to predict when
# entries appear in the DDT ZAP
log_must save_tunable DEDUP_LOG_TXG_MAX
log_must set_tunable32 DEDUP_LOG_TXG_MAX 1

function cleanup
{
	if poolexists $TESTPOOL ; then
		destroy_pool $TESTPOOL
	fi
	log_must restore_tunable DEDUP_LOG_TXG_MAX
}

function ddt_entries
{
	typeset -i entries=$(zpool status -D $TESTPOOL | \
		grep "dedup: DDT entries" | awk '{print $4}')

	echo ${entries}
}

log_onexit cleanup

log_must zpool create -f -o feature@block_cloning=disabled $TESTPOOL $DISKS

log_must zfs create -o recordsize=512 -o dedup=on $TESTPOOL/$TESTFS
typeset mountpoint=$(get_prop mountpoint $TESTPOOL/$TESTFS)
log_must dd if=/dev/urandom of=$mountpoint/f1 bs=512k count=1
log_must cp $mountpoint/f1 $mountpoint/f2
sync_pool $TESTPOOL
entries=$(ddt_entries)
log_note "ddt entries before: $entries"

log_must zpool ddtprune -p 100 $TESTPOOL
sync_pool $TESTPOOL
new_entries=$(ddt_entries)
[[ "$entries" -eq "$new_entries" ]] || \
	log_fail "DDT entries changed from $entries to $new_entries"

log_must truncate -s 128k $mountpoint/f2
sync_pool $TESTPOOL
sleep 1
log_must zpool ddtprune -p 100 $TESTPOOL
sync_pool $TESTPOOL

new_entries=$(ddt_entries)
[[ "$((entries / 4))" -eq "$new_entries" ]] || \
	log_fail "DDT entries did not shrink enough: $entries -> $new_entries"


log_pass "DDT pruning correctly removes non-duplicate entries"
