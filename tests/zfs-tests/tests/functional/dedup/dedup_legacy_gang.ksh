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

# DESCRIPTION:
#	Verify that deduplicated gang blocks in a traditional (legacy) DDT
#	are refcounted correctly.  A gang header is stored in more copies
#	than the data it gangs, so its BP carries more DVAs than the
#	copies= value the block was written with, and the free-time and
#	clone-time DDT phys selection must match by block identity, not by
#	the BP's DVA count.  Getting this wrong bypassed the refcount:
#	each reference delete physically freed the shared gang header (a
#	committed double-free from the second delete on), leaked the gang
#	members and left a stale DDT entry behind.
#
# STRATEGY:
#	1. Create a pool with feature@fast_dedup disabled (traditional DDT)
#	   and a dedup dataset.
#	2. Force ganging and write a file of unique blocks; verify every
#	   block is a two-DVA gang BP stored in the copies=1 phys slot.
#	3. Create a second reference via dedup (copy) and a third via
#	   block cloning; verify each bumps the refcount of that same
#	   phys slot, and that the clone took no BRT reference.
#	4. Delete the references one at a time; verify the refcount drops
#	   by exactly one each time and a survivor is still readable
#	   after a fresh import.
#	5. After the last delete, verify the DDT is empty and zdb reports
#	   no leaked space or double allocations.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/gang_blocks/gang_blocks.kshlib

verify_runnable "global"

log_assert "Deduplicated gang blocks in a legacy DDT are refcounted correctly"

log_must save_tunable METASLAB_FORCE_GANGING
log_must save_tunable METASLAB_FORCE_GANGING_PCT
log_must save_tunable BCLONE_ENABLED

function cleanup
{
	if poolexists $TESTPOOL ; then
		destroy_pool $TESTPOOL
	fi
	log_must restore_tunable METASLAB_FORCE_GANGING
	log_must restore_tunable METASLAB_FORCE_GANGING_PCT
	log_must restore_tunable BCLONE_ENABLED
}

log_onexit cleanup

# Count DDT entries whose copies=1 phys slot holds a two-DVA gang BP with
# the given refcount - the shape whose refcounting this test verifies.
# A zdb failure yields a non-numeric result, failing the caller's test.
function count_gang_phys1
{
	typeset refcnt=$1
	typeset out

	out=$(zdb -DDDDD $TESTPOOL) || { echo "zdb failed"; return; }
	echo "$out" | grep -c "refcnt $refcnt phys 1 .*gang.*double"
}

# We disable compression so our writes create predictable results on disk,
# and use xattr=sa to prevent selinux xattrs influencing our accounting.
# ashift=9 keeps gang headers small.
log_must zpool create -f \
    -o ashift=9 \
    -o feature@fast_dedup=disabled \
    -o feature@block_cloning=enabled \
    -O dedup=on \
    -O compression=off \
    -O xattr=sa \
    $TESTPOOL $DISKS

log_must test "$(get_pool_prop feature@fast_dedup $TESTPOOL)" = "disabled"

# redundant_metadata=all (the default) stores gang headers in copies+1
# copies; pin it so the copies=1 blocks below always get a two-DVA gang
# header BP.
log_must zfs create -o recordsize=128k -o redundant_metadata=all \
    $TESTPOOL/$TESTFS
typeset mountpoint=$(get_prop mountpoint $TESTPOOL/$TESTFS)

# Ensure block cloning is enabled (it is on by default) so the
# FICLONE call below succeeds instead of failing with EOPNOTSUPP.
log_must set_tunable32 BCLONE_ENABLED 1

# Write unique data with ganging forced: 4 blocks, each a dedup gang block
# whose gang header BP carries more DVAs than copies=1. The allocation (and
# so the ganging decision) happens at sync, so keep the tunable set until
# the pool has synced.
log_must set_tunable64 METASLAB_FORCE_GANGING 20000
log_must set_tunable32 METASLAB_FORCE_GANGING_PCT 100
log_must dd if=/dev/urandom of=$mountpoint/file1 bs=128k count=4
sync_pool $TESTPOOL
log_must set_tunable32 METASLAB_FORCE_GANGING_PCT 0

typeset dva=$(get_first_block_dva $TESTPOOL/$TESTFS file1)
check_is_gang_dva $dva

# All four entries must have the hazardous shape: a two-DVA gang BP in
# the copies=1 slot.
log_must eval "zdb -D $TESTPOOL | grep -q 'DDT-sha256-zap-unique:.*entries=4'"
log_must test "$(count_gang_phys1 1)" -eq 4

typeset sum=$(xxh128digest $mountpoint/file1)

# Second reference via dedup: same phys slot, refcount 2.
log_must dd if=$mountpoint/file1 of=$mountpoint/file2 bs=128k
sync_pool $TESTPOOL
log_must eval "zdb -D $TESTPOOL | grep -q 'DDT-sha256-zap-duplicate:.*entries=4'"
log_must test "$(count_gang_phys1 2)" -eq 4

# Third reference via block cloning: ddt_addref() must bump the same
# phys slot to 3; nothing may land in the BRT.  Use FICLONE (-c):
# unlike copy_file_range it cannot silently fall back to a byte copy,
# which would dedup to the same refcount without exercising the clone
# path.
log_must clonefile -c $mountpoint/file1 $mountpoint/file3
sync_pool $TESTPOOL
log_must test "$(get_pool_prop bcloneused $TESTPOOL)" -eq 0
log_must test "$(count_gang_phys1 3)" -eq 4

# Delete the references one at a time. Each delete must drop the
# refcount by exactly one; the shared gang blocks stay allocated until
# the last one.
log_must rm $mountpoint/file1
sync_pool $TESTPOOL
log_must test "$(count_gang_phys1 2)" -eq 4

# A survivor must still be readable from disk after a fresh import.
log_must zpool export $TESTPOOL
log_must zpool import $TESTPOOL
log_must test "$(xxh128digest $mountpoint/file2)" = "$sum"

log_must rm $mountpoint/file3
sync_pool $TESTPOOL
log_must test "$(count_gang_phys1 1)" -eq 4
log_must test "$(xxh128digest $mountpoint/file2)" = "$sum"

log_must rm $mountpoint/file2
sync_pool $TESTPOOL

# The last delete must have released the entries and freed the gang
# members: empty DDT, no leaked space, no double frees.
log_must eval "zdb -D $TESTPOOL | grep -q 'All DDTs are empty'"

zdb_out=$(zdb -bcc $TESTPOOL 2>&1)
typeset rc=$?
echo "$zdb_out"
if (( rc != 0 )) || \
    echo "$zdb_out" | grep -Eq "leaked space|DOUBLE ALLOC|DOUBLE FREE"; then
	log_fail "legacy dedup gang free leaked or double-freed space"
fi

log_pass "Deduplicated gang blocks in a legacy DDT are refcounted correctly"
