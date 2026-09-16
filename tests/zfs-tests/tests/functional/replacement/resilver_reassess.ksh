#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source. A copy of the CDDL is also available via the Internet at
# https://opensource.org/license/CDDL-1.0.
#

# shellcheck disable=SC1091

. "$STF_SUITE"/include/libtest.shlib
. "$STF_SUITE"/tests/functional/replacement/replacement.cfg

#
# DESCRIPTION:
#	Healing replacements resume when a rebuild or checkpoint stops blocking
#	their progress.
#
# STRATEGY:
#	1. Bring an incomplete replacement online during a sequential rebuild.
#	2. Cancel or finish the rebuild with post-rebuild scrubs disabled.
#	3. Repeat with another rebuild still active at cancellation.
#	4. Complete a healing resilver under a checkpoint, then discard it.
#	5. Discard a checkpoint while a newer missed write needs leaf deferral.
#	6. In each case, verify replacement completion and cold data integrity
#	   without requesting a scrub or resilver.
#

verify_runnable "global"

function cleanup
{
	zinject -c all >/dev/null 2>&1
	zpool checkpoint -d -w "$TESTPOOL1" >/dev/null 2>&1
	# Destroy before restoring progress so cleanup cannot finish repair.
	destroy_pool "$TESTPOOL1"
	set_tunable32 SCAN_SUSPEND_PROGRESS "$orig_suspend"
	set_tunable32 REBUILD_SCRUB_ENABLED "$orig_scrub"
	rm -rf "$workdir"
}

function check_rebuild_history # message count
{
	typeset history count
	history=$(zpool history -i "$TESTPOOL1") || return 1
	count=$(echo "$history" |
	    awk -v msg="$1" '$0 ~ / rebuild / && $NF == msg { n++ }
		END { print n + 0 }')
	[[ "$count" == "$2" ]] ||
	    log_fail "expected $2 rebuild $1 events, found $count"
}

log_assert "Pending healing replacements resume after rebuilds and checkpoints"

orig_suspend=$(get_tunable SCAN_SUSPEND_PROGRESS)
orig_scrub=$(get_tunable REBUILD_SCRUB_ENABLED)
workdir=$(mktemp -d "$TEST_BASE_DIR/resilver_reassess.XXXXXX") ||
    log_fail "cannot create test directory"
log_onexit cleanup

log_must zinject -c all
log_must truncate -s 512M "$workdir"/disk-{0..5}
log_must dd if=/dev/urandom of="$workdir/expected" bs=1M count=8
# A post-rebuild scrub must not supply the healing notification being tested.
log_must set_tunable32 REBUILD_SCRUB_ENABLED 0

for finish in cancel complete multiple checkpoint deferred; do
	log_note "Reassess after $finish"
	if [[ "$finish" == checkpoint || "$finish" == deferred ]]; then
		log_must zpool create -f "$TESTPOOL1" "$workdir/disk-0"
	else
		log_must zpool create -f -o feature@resilver_defer=disabled \
		    "$TESTPOOL1" "$workdir"/disk-{0..2}
	fi
	log_must zfs create -o compression=off -o atime=off "$TESTPOOL1/$TESTFS"
	mntpnt=$(get_prop mountpoint "$TESTPOOL1/$TESTFS")
	log_must cp "$workdir/expected" "$mntpnt/file"
	sync_pool "$TESTPOOL1"
	log_must set_tunable32 SCAN_SUSPEND_PROGRESS 1

	if [[ "$finish" == checkpoint || "$finish" == deferred ]]; then
		log_must zpool replace "$TESTPOOL1" \
		    "$workdir/disk-0" "$workdir/disk-3"
		log_must is_pool_resilvering "$TESTPOOL1"
		log_must zpool checkpoint "$TESTPOOL1"
		if [[ "$finish" == deferred ]]; then
			# Extend missing writes beyond the scan's maximum.
			# Discard must defer the eligible leaf, not the root.
			log_must zinject -d "$workdir/disk-3" -e io -T write \
			    -f 100 "$TESTPOOL1"
			log_must cp "$workdir/expected" "$mntpnt/new-file"
			sync_pool "$TESTPOOL1"
			log_must zinject -c all
			log_must zpool checkpoint -d -w "$TESTPOOL1"
		fi
		log_must set_tunable32 SCAN_SUSPEND_PROGRESS 0
		if [[ "$finish" == checkpoint ]]; then
			# A checkpoint prevents this pass from retiring DTLs.
			# Discard must start healing after that pass finishes.
			log_must zpool wait -t resilver "$TESTPOOL1"
			log_must is_pool_resilvered "$TESTPOOL1"
			log_must is_pool_replacing "$TESTPOOL1"
			log_must zpool checkpoint -d -w "$TESTPOOL1"
		fi
	else
		# Failed repair leaves a replacement to bring back online.
		log_must zpool replace "$TESTPOOL1" \
		    "$workdir/disk-0" "$workdir/disk-3"
		log_must zinject -d "$workdir/disk-3" -e io -T write \
		    -f 100 "$TESTPOOL1"
		log_must set_tunable32 SCAN_SUSPEND_PROGRESS 0
		log_must zpool wait -t resilver "$TESTPOOL1"
		log_must zinject -c all
		log_must is_pool_replacing "$TESTPOOL1"
		log_must zpool offline "$TESTPOOL1" "$workdir/disk-3"
		log_must set_tunable32 SCAN_SUSPEND_PROGRESS 1
		log_must zpool replace -s "$TESTPOOL1" \
		    "$workdir/disk-1" "$workdir/disk-4"
		if [[ "$finish" == multiple ]]; then
			log_must zpool replace -s "$TESTPOOL1" \
			    "$workdir/disk-2" "$workdir/disk-5"
			log_must check_rebuild_history started 2
		else
			log_must check_rebuild_history started 1
		fi
		# Rebuilding still blocks the online request's healing work.
		# With two rebuilds, canceling one must not lose that work while
		# the second remains active.
		log_must zpool online "$TESTPOOL1" "$workdir/disk-3"
		if [[ "$finish" != complete ]]; then
			log_must zpool detach "$TESTPOOL1" "$workdir/disk-4"
			log_must check_rebuild_history canceled 1
		fi
		log_must set_tunable32 SCAN_SUSPEND_PROGRESS 0
	fi

	# Waiting on topology also waits for an asynchronously scheduled scan.
	log_must zpool wait -t replace "$TESTPOOL1"
	log_mustnot is_pool_replacing "$TESTPOOL1"
	log_must is_pool_resilvered "$TESTPOOL1"
	# The original is detached; import also removes cached file data.
	log_must zpool export "$TESTPOOL1"
	log_must zpool import -d "$workdir" "$TESTPOOL1"
	log_must cmp "$workdir/expected" "$mntpnt/file"
	if [[ "$finish" == deferred ]]; then
		log_must cmp "$workdir/expected" "$mntpnt/new-file"
	fi
	log_must check_pool_status "$TESTPOOL1" "errors" "No known data errors"
	log_must zdb -cdui "$TESTPOOL1/$TESTFS"
	destroy_pool "$TESTPOOL1"
done

log_pass "Pending healing replacements resume after rebuilds and checkpoints"
