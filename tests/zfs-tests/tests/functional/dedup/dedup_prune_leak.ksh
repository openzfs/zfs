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
# Copyright (c) 2025, Nutanix Inc.
#

# DESCRIPTION:
#	Verify that zpool ddtprune successfully reduces the number of entries
#	in the DDT.
#
# STRATEGY:
#	1. Create a pool with dedup=on
#	2. Add non-duplicate entries to the DDT
#	3. ddtprune all entries
#	4. Remove the file
#	5. Verify there's no space leak
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/events/events_common.kshlib

verify_runnable "both"

log_assert "Verify DDT pruning does not cause space leak"

# We set the dedup log txg interval to 1, to get a log flush every txg,
# effectively disabling the log. Without this it's hard to predict when
# entries appear in the DDT ZAP
log_must save_tunable DEDUP_LOG_TXG_MAX
log_must set_tunable32 DEDUP_LOG_TXG_MAX 1
log_must save_tunable DEDUP_LOG_FLUSH_ENTRIES_MIN
log_must set_tunable32 DEDUP_LOG_FLUSH_ENTRIES_MIN 100000
function cleanup
{
	if poolexists $TESTPOOL ; then
		destroy_pool $TESTPOOL
	fi
	log_must restore_tunable DEDUP_LOG_TXG_MAX
	log_must restore_tunable DEDUP_LOG_FLUSH_ENTRIES_MIN
}

log_onexit cleanup

log_must zpool create -f $TESTPOOL $DISKS

log_must zfs create -o dedup=on $TESTPOOL/$TESTFS
typeset mountpoint=$(get_prop mountpoint $TESTPOOL/$TESTFS)
log_must dd if=/dev/urandom of=$mountpoint/f1 bs=1M count=16
# We seems to need some amount of txg sync here to make it more consistently
# reproducible
for i in $(seq 50); do
	zpool sync $TESTPOOL
done

log_must zpool ddtprune -p 100 $TESTPOOL
log_must rm $mountpoint/f1
sync_pool $TESTPOOL

zdb_out=$(zdb -bcc $TESTPOOL)
echo "$zdb_out"
if echo "$zdb_out" | grep -q "leaked space"; then
	log_fail "DDT pruning causes space leak"
fi

log_pass "DDT pruning does not cause space leak"
