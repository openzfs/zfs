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
#	6. Repeat with ganging forced, so the pruned blocks are gang blocks:
#	   freeing them must release the gang members too, not just the
#	   headers
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
log_must save_tunable METASLAB_FORCE_GANGING
log_must save_tunable METASLAB_FORCE_GANGING_PCT
function cleanup
{
	if poolexists $TESTPOOL ; then
		destroy_pool $TESTPOOL
	fi
	log_must restore_tunable DEDUP_LOG_TXG_MAX
	log_must restore_tunable DEDUP_LOG_FLUSH_ENTRIES_MIN
	log_must restore_tunable METASLAB_FORCE_GANGING
	log_must restore_tunable METASLAB_FORCE_GANGING_PCT
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

zdb_out=$(zdb -bcc $TESTPOOL 2>&1)
rc=$?
echo "$zdb_out"
if (( rc != 0 )) || echo "$zdb_out" | grep -q "leaked space"; then
	log_fail "DDT pruning causes space leak"
fi

# Repeat with ganging forced: freeing a pruned gang block must release
# the gang members as well, not only the gang header. The allocation
# (and so the ganging decision) happens at sync, so keep the tunable
# set until the pool has synced. The extra syncs let the DDT log swap
# and flush into the ZAP: the entry count includes log-resident entries,
# but the prune walker only traverses the stored objects, so entries
# stay unprunable until one flush after their append txg.
log_must set_tunable64 METASLAB_FORCE_GANGING 20000
log_must set_tunable32 METASLAB_FORCE_GANGING_PCT 100
log_must dd if=/dev/urandom of=$mountpoint/f2 bs=1M count=16
for i in $(seq 5); do
	sync_pool $TESTPOOL
done
log_must set_tunable32 METASLAB_FORCE_GANGING_PCT 0

# Verify the precondition: all 128 new entries are present in the DDT
# and their blocks are ganged.
log_must test "$(zdb -DDDDD $TESTPOOL | grep 'phys 0 ' | grep -c gang)" \
    -eq 128
typeset entries=$(zpool status -D $TESTPOOL | \
    grep "dedup: DDT entries" | awk '{print $4+0}')
log_must test "$entries" -eq 128

# Sleep 1s so the DDT entries are at least 1 second old.  ddtprune uses
# an age-based cutoff and will silently skip entries that are too fresh.
sleep 1

log_must zpool ddtprune -p 100 $TESTPOOL
sync_pool $TESTPOOL

# Confirm the prune actually removed the entries.
entries=$(zpool status -D $TESTPOOL | \
    grep "dedup: DDT entries" | awk '{print $4+0}')
[[ -z "$entries" || "$entries" -eq 0 ]] || \
    log_fail "DDT entries not pruned: $entries remain"

log_must rm $mountpoint/f2
sync_pool $TESTPOOL

zdb_out=$(zdb -bcc $TESTPOOL 2>&1)
rc=$?
echo "$zdb_out"
if (( rc != 0 )) || \
    echo "$zdb_out" | grep -Eq "leaked space|DOUBLE ALLOC|DOUBLE FREE"; then
	log_fail "DDT pruning causes space leak on gang blocks"
fi

log_pass "DDT pruning does not cause space leak"
