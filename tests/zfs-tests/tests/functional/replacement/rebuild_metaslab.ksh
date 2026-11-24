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

. "$STF_SUITE/include/libtest.shlib"

#
# DESCRIPTION:
# A sequential rebuild copies an entire metaslab when its allocation map
# cannot be loaded, rather than remaining active without making progress.
#
# STRATEGY:
# 1. Create a mirror or dRAID pool and write data.
# 2. Export and inject a truncated two-word space-map entry.
# 3. Replace a leaf with sequential reconstruction and the scrub disabled.
# 4. Export after partial progress, verify a persisted nonzero offset,
#    import with reconstruction paused, then resume to completion.
# 5. Remove the original leaf and another source. Import read-only and
#    verify the data, relying on the replacement rather than redundancy.
#

verify_runnable "global"

typeset old_preload
typeset old_debug_load
typeset old_scrub
typeset old_suspend
typeset old_limit
typeset handler
typeset top_zap
typeset workdir

old_preload=$(get_tunable METASLAB_PRELOAD_ENABLED)
old_debug_load=$(get_tunable METASLAB_DEBUG_LOAD)
old_scrub=$(get_tunable REBUILD_SCRUB_ENABLED)
old_suspend=$(get_tunable SCAN_SUSPEND_PROGRESS)
old_limit=$(get_tunable REBUILD_VDEV_LIMIT)
workdir=$(mktemp -d "$TEST_BASE_DIR/rebuild_metaslab.XXXXXX") ||
    log_fail "cannot create test directory"

function cleanup
{
	if [[ -n "$handler" ]]; then
		log_must zinject -c "$handler"
	fi
	log_must set_tunable32 SCAN_SUSPEND_PROGRESS "$old_suspend"
	log_must set_tunable32 REBUILD_SCRUB_ENABLED "$old_scrub"
	log_must set_tunable64 REBUILD_VDEV_LIMIT "$old_limit"
	log_must set_tunable64 METASLAB_PRELOAD_ENABLED "$old_preload"
	log_must set_tunable64 METASLAB_DEBUG_LOAD "$old_debug_load"
	if poolexists "$TESTPOOL1"; then
		log_must_busy zpool export -f "$TESTPOOL1"
	fi
	rm -rf "$workdir"
}

function rebuild_has_progress
{
	zpool status -jp --json-int "$TESTPOOL1" > "$workdir/status" || return 1
	jq -e --arg pool "$TESTPOOL1" '
	    any(.pools[$pool].scan_stats.rebuild_stats[];
	        .state == "ACTIVE" and .issued > 0)
	' "$workdir/status" > /dev/null
}

function wait_for_progress
{
	typeset -i attempt
	for ((attempt = 0; attempt < 600; attempt++)); do
		rebuild_has_progress && return 0
		sleep 0.1
	done
	cat "$workdir/status"
	return 1
}

function rebuild_has_saved_offset
{
	awk '
	    $1 == "org.openzfs:vdev_rebuild" && $2 == "=" {
	        found = 1
	        if ($3 != 1 || $4 <= 0)
	            bad = 1
	    }
	    END { exit !found || bad }
	' "$1"
}

function wait_for_rebuild
{
	typeset -i attempt

	for ((attempt = 0; attempt < 120; attempt++)); do
		if zpool history -i "$TESTPOOL1" |
		    grep -q ' rebuild .* complete'; then
			return 0
		fi
		sleep 1
	done
	zpool status -v "$TESTPOOL1"
	return 1
}

log_assert "A rebuild completes despite an unavailable allocation map"
log_onexit cleanup

log_must set_tunable64 METASLAB_PRELOAD_ENABLED 0
log_must set_tunable64 METASLAB_DEBUG_LOAD 0
log_must set_tunable32 REBUILD_SCRUB_ENABLED 0
log_must set_tunable64 REBUILD_VDEV_LIMIT $((1024 * 1024))
# Populate several metaslabs so the injector can avoid active allocators.
log_must dd if=/dev/urandom of="$workdir/reference" bs=1M count=128

for layout in mirror draid1:2d:3c:0s; do
	typeset dir="$workdir/$layout"
	typeset -a leaves
	log_must mkdir -p "$dir/away"
	set -A leaves -- "$dir/d0" "$dir/d1"
	if [[ "$layout" != mirror ]]; then
		leaves+=("$dir/d2")
	fi
	log_must truncate -s 512M "${leaves[@]}" "$dir/new"
	log_must zpool create -o feature@log_spacemap=disabled \
	    -O compression=off "$TESTPOOL1" "$layout" "${leaves[@]}"
	log_must cp "$workdir/reference" "/$TESTPOOL1/data"
	sync_pool "$TESTPOOL1"
	log_must_busy zpool export "$TESTPOOL1"
	# Import-time frees can temporarily make populated maps ineligible.
	log_must_retry "no suitable metaslab range" 5 zhack -d "$dir" \
	    -o metaslab_preload_enabled=0 -o spa_num_allocators=1 \
	    metaslab corrupt "$TESTPOOL1" truncated-entry ||
	    log_fail "cannot inject a truncated space-map entry"
	log_must zpool import -d "$dir" "$TESTPOOL1"

	log_must set_tunable32 SCAN_SUSPEND_PROGRESS 1
	log_must zpool replace -s "$TESTPOOL1" "$dir/d0" "$dir/new"
	# One delayed writer bounds progress while we observe the first I/Os.
	log_must zinject -d "$dir/new" -D 100:1 -T write "$TESTPOOL1" \
	    > "$dir/injection"
	handler=$(awk '/^Added handler/ { print $3 }' "$dir/injection")
	[[ "$handler" == +([0-9]) ]] || log_fail "missing delay handler"
	log_must set_tunable32 SCAN_SUSPEND_PROGRESS 0
	log_must wait_for_progress
	log_must set_tunable32 SCAN_SUSPEND_PROGRESS 1
	log_must zinject -c "$handler"
	handler=
	log_must_busy zpool export "$TESTPOOL1"

	# Inspect the stable top-level ZAP, not unsynced in-core scan counters.
	log_must zdb -e -p "$dir" -C "$TESTPOOL1" > "$dir/config"
	top_zap=$(awk '$1 == "com.delphix:vdev_zap_top:" { print $2 }' \
	    "$dir/config")
	[[ "$top_zap" == +([0-9]) ]] || log_fail "missing top-level ZAP"
	log_must zdb -e -p "$dir" -dddd "$TESTPOOL1" "$top_zap" \
	    > "$dir/rebuild"
	log_must rebuild_has_saved_offset "$dir/rebuild"
	log_must zpool import -d "$dir" "$TESTPOOL1"
	log_must rebuild_has_progress
	log_must set_tunable32 SCAN_SUSPEND_PROGRESS 0
	log_must wait_for_rebuild
	log_must zpool wait -t resilver "$TESTPOOL1"
	log_must_busy zpool export "$TESTPOOL1"

	# The mirror must read the new leaf. dRAID has no spare parity left
	# to reconstruct data missing from the replacement.
	log_must mv "$dir/d0" "$dir/d1" "$dir/away"
	log_must zpool import -o readonly=on -d "$dir" "$TESTPOOL1"
	log_must cmp "$workdir/reference" "/$TESTPOOL1/data"
	log_must_busy zpool export "$TESTPOOL1"
	log_must zdb -e -p "$dir" -L -bcc "$TESTPOOL1"
done

log_pass "A rebuild completes despite an unavailable allocation map"
