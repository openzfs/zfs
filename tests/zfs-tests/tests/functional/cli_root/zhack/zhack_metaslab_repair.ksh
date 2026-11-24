#!/bin/ksh
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

# ZTS supplies STF_SUITE and TESTPOOL.
# shellcheck disable=SC2154

#
# Verify that repairable metaslab damage is conservatively replaced with a
# canonical space map and malformed maps remain unavailable.
# Keep each case's import directory separate from earlier, destroyed pools.
#

. "$STF_SUITE"/include/libtest.shlib

verify_runnable "global"

typeset old_debug_load
typeset old_preload
typeset old_keep_logs
typeset corruption
typeset tmpdir

function cleanup
{
	log_must set_tunable64 METASLAB_DEBUG_LOAD "$old_debug_load"
	log_must set_tunable64 METASLAB_PRELOAD_ENABLED "$old_preload"
	log_must set_tunable64 KEEP_LOG_SPACEMAPS_AT_EXPORT "$old_keep_logs"
	if poolexists "$TESTPOOL"; then
		destroy_pool "$TESTPOOL"
	fi
	rm -rf "$tmpdir"
}

function repair_events
{
	zpool events -H "$1" | awk '
	    /sysevent.fs.zfs.metaslab_repair/ { count++ }
	    END { print count + 0 }'
}

function inject_damage
{
	typeset output=$1
	shift
	zhack -o metaslab_preload_enabled=0 -o spa_num_allocators=1 \
	    "$@" > "$output"
}

function wait_for_repair
{
	typeset pool=$1
	typeset -i attempt

	for ((attempt = 0; attempt < 10; attempt++)); do
		if zpool history -il "$pool" | grep -q "metaslab repair"; then
			return 0
		fi
		zpool sync "$pool" || return 1
	done

	zpool history -il "$pool" | grep -q "metaslab repair"
}

function run_allocated_map_test
{
	typeset dir="$tmpdir/allocated-map"
	typeset vdev="$dir/vdev"
	typeset allocation="$dir/allocation"
	typeset allocated_map="$dir/allocated-map"
	typeset leak_log="$dir/leak-error"
	typeset -i ms_shift ms_size

	log_note "testing allocated-map boundaries at nonzero metaslab offsets"
	log_must mkdir "$dir"
	log_must mkfile -n 128m "$vdev"
	log_must zpool create -o feature@log_spacemap=disabled -o ashift=12 \
	    "$TESTPOOL" "$vdev"
	log_must_busy zpool export "$TESTPOOL"
	log_must zdb -e -p "$dir" -m "$TESTPOOL" > "$allocated_map"
	ms_shift=$(awk '$1 == "vdev" { print $5; exit }' "$allocated_map")
	((ms_shift >= 12 && ms_shift <= 25)) ||
	    log_fail "unexpected metaslab shift: $ms_shift"
	((ms_size = 1 << ms_shift))

	# An error before applying ranges must not report partial application.
	log_must printf '\tvdev 9999\t  metaslab shift %d\n' "$ms_shift" \
	    > "$allocation"
	zhack -d "$dir" metaslab leak "$TESTPOOL" < "$allocation" \
	    > "$leak_log" 2>&1
	log_must test $? -eq 1
	log_must grep -q "no such vdev with id 9999" "$leak_log"
	log_mustnot grep -q "earlier changes may already be committed" "$leak_log"

	# Reserve the tails without writing their data.
	{
		log_must printf '\tvdev 0\t  metaslab shift %d\n' "$ms_shift"
		log_must printf 'ALLOC: %d %d\n' "$((ms_size - 4096))" 4096
		log_must printf 'ALLOC: %d %d\n' "$((3 * ms_size - 4096))" 4096
		log_must printf '\tvdev 9999\t  metaslab shift %d\n' "$ms_shift"
	} > "$allocation"
	zhack -d "$dir" metaslab leak "$TESTPOOL" < "$allocation" \
	    > "$leak_log" 2>&1
	log_must test $? -eq 1
	log_must grep -q "no such vdev with id 9999" "$leak_log"
	log_must grep -q "earlier changes may already be committed" "$leak_log"

	# Reapplying the same reservations assigns transactions but changes no map.
	zhack -d "$dir" metaslab leak "$TESTPOOL" < "$allocation" \
	    > "$leak_log" 2>&1
	log_must test $? -eq 1
	log_must grep -q "no such vdev with id 9999" "$leak_log"
	log_mustnot grep -q "earlier changes may already be committed" "$leak_log"
	log_must zdb -e -p "$dir" -m --allocated-map "$TESTPOOL" \
	    0 0 2 > "$allocated_map"
	log_must cat "$allocated_map"

	# Each range must fit its metaslab, including the final allocation.
	log_must awk -v ms_size="$ms_size" "
	    \$1 == \"metaslab\" {
	        ms = \$2
	        last_end = ms * ms_size
	    }
	    \$1 == \"ALLOC:\" {
	        start = \$2
	        size = \$3
	        end = (ms + 1) * ms_size
	        if (size <= 0 || start < last_end || start >= end ||
	            size > end - start) {
	            bad = 1
	            exit
	        }
	        count[ms]++
	        last_end = start + size
	        tail[ms] = last_end == end
	    }
	    END {
	        exit bad || count[0] == 0 || count[2] == 0 ||
	            !tail[0] || !tail[2]
	    }
	" "$allocated_map"
	log_must zpool import -d "$dir" "$TESTPOOL"
	log_must zpool destroy "$TESTPOOL"
}

function run_repair_test
{
	typeset corruption=$1
	typeset log_spacemap=$2
	typeset loader=${3:-import}
	typeset dir="$tmpdir/$corruption-$log_spacemap-$loader"
	typeset checksum
	typeset repaired_checksum
	typeset injection
	typeset injection_log="$dir/injection"
	typeset allocated_map="$dir/map"
	typeset check_log="$dir/check"
	typeset status_log="$dir/status"
	typeset -i check_status
	typeset vdev
	typeset offset
	typeset size
	typeset events_before
	typeset -i data_mb=16
	typeset -a vdevs
	typeset -a import_args
	typeset -a zdb_args

	log_must mkdir "$dir"
	set -A vdevs -- "$dir/vdev"
	set -A import_args -- -d "$dir"
	set -A zdb_args -- -p "$dir"
	if [[ "$loader" == "import" ]]; then
		# Populate several small metaslabs, including inactive maps.
		vdevs+=("$dir/vdev-1")
		log_must mkfile -n 512m "${vdevs[@]}"
		data_mb=128
	else
		# Bound initialize/TRIM; leave empty metaslabs for the sync tests.
		if [[ "$loader" == "zhack" ]]; then
			if [[ "$corruption" == "passivate-repair" ]]; then
				vdevs+=("$dir/vdev-1")
			fi
			log_must mkfile -n 4g "${vdevs[@]}"
		else
			log_must mkfile 128m "${vdevs[0]}"
		fi
		log_must set_tunable64 METASLAB_PRELOAD_ENABLED 0
	fi

	log_note "testing $corruption corruption with log_spacemap " \
	    "$log_spacemap, loaded by $loader"
	log_must zpool create -o feature@log_spacemap="$log_spacemap" \
	    -O compression=off "$TESTPOOL" "${vdevs[@]}"
	events_before=$(repair_events "$TESTPOOL")
	log_must dd if=/dev/urandom of="/$TESTPOOL/data" bs=1M count="$data_mb"
	checksum=$(cksum "/$TESTPOOL/data")
	log_must zpool sync "$TESTPOOL"
	log_must dd if=/dev/urandom of="/$TESTPOOL/churn" bs=128k count=10
	log_must zpool sync "$TESTPOOL"
	if [[ "$corruption" == "passivate-repair" ]]; then
		# The injector must skip the inactive group before changing any map.
		log_must zpool set allocating=off "$TESTPOOL" "${vdevs[0]}"
	fi

	# Retry only when import-time activity temporarily excludes all maps.
	if [[ "$corruption" == "log-duplicate-free" ]]; then
		# Start with a flushed map, then leave only the duplicate in a log.
		log_must set_tunable64 KEEP_LOG_SPACEMAPS_AT_EXPORT 0
		log_must_busy zpool export "$TESTPOOL"
		log_must set_tunable64 KEEP_LOG_SPACEMAPS_AT_EXPORT 1
		log_must_retry "no suitable metaslab range" 5 \
		    inject_damage "$injection_log" "${import_args[@]}" \
		    -o zfs_keep_log_spacemaps_at_export=1 \
		    -o metaslab_debug_unload=0 \
		    metaslab corrupt "$TESTPOOL" "$corruption" ||
		    log_fail "cannot inject $corruption"
	elif [[ "$log_spacemap" == "enabled" ]]; then
		log_must set_tunable64 KEEP_LOG_SPACEMAPS_AT_EXPORT 1
		log_must_busy zpool export "$TESTPOOL"
		log_must_retry "no suitable metaslab range" 5 \
		    inject_damage "$injection_log" "${import_args[@]}" \
		    -o zfs_keep_log_spacemaps_at_export=1 \
		    metaslab corrupt "$TESTPOOL" "$corruption" ||
		    log_fail "cannot inject $corruption"
	else
		log_must set_tunable64 KEEP_LOG_SPACEMAPS_AT_EXPORT 0
		log_must_busy zpool export "$TESTPOOL"
		log_must_retry "no suitable metaslab range" 5 \
		    inject_damage "$injection_log" "${import_args[@]}" \
		    metaslab corrupt "$TESTPOOL" "$corruption" ||
		    log_fail "cannot inject $corruption"
	fi

	if [[ "$corruption" == "invalid-summary" ]]; then
		log_must zdb -e "${zdb_args[@]}" -m "$TESTPOOL" \
		    > "$allocated_map"
		log_must grep -Eq 'metaslab .*free[[:space:]]+invalid$' \
		    "$allocated_map"
		log_must set_tunable64 METASLAB_PRELOAD_ENABLED 0
	fi

	# Before condensation, loaded headers must match the reconstructed map.
	if [[ "$log_spacemap" == "disabled" ]]; then
		log_must zdb -e "${zdb_args[@]}" -Pm --allocated-map \
		    "$TESTPOOL" > "$allocated_map"
		log_must awk "
		    \$1 == \"vdev\" { ms_size = 2 ^ \$5 }
		    \$1 == \"metaslab\" {
		        if (seen && free + allocated != size)
		            bad = 1
		        free = \$NF
		        size = ms_size
		        allocated = 0
		        seen++
		        if (free !~ /^[0-9]+$/)
		            bad = 1
		    }
		    \$1 == \"ALLOC:\" { allocated += \$3 }
		    END { exit !seen || bad || free + allocated != size }
		" "$allocated_map"
	fi

	# Invalid summaries must repair without normal or debug preloading.
	if [[ "$corruption" == "invalid-summary" || "$loader" != "import" ]]; then
		log_must set_tunable64 METASLAB_DEBUG_LOAD 0
	else
		# Load logged metaslabs so import finds the injected overlap.
		log_must set_tunable64 METASLAB_DEBUG_LOAD 1
	fi
	log_must zpool import "${import_args[@]}" "$TESTPOOL"
	if [[ "$loader" == "initialize" ]]; then
		log_must zpool initialize -w "$TESTPOOL"
		log_must zpool status -i "$TESTPOOL" > "$status_log"
		log_must grep -q completed "$status_log"
	elif [[ "$loader" == "trim" ]]; then
		log_must zpool trim -w "$TESTPOOL"
		log_must zpool status -t "$TESTPOOL" > "$status_log"
		log_must grep -q completed "$status_log"
	fi

	log_must wait_for_repair "$TESTPOOL"
	# libzpool records pool history but cannot emit kernel zevents.
	if [[ "$loader" != "zhack" ]]; then
		log_must test "$(repair_events "$TESTPOOL")" -gt "$events_before"
	fi

	log_must set_tunable64 METASLAB_DEBUG_LOAD 0
	log_must set_tunable64 KEEP_LOG_SPACEMAPS_AT_EXPORT 0
	log_must_busy zpool export "$TESTPOOL"

	# Strict replay must not abort, and referenced blocks must checksum.
	# Quarantined free bytes are intentionally leaked (zdb exits 2).
	# Reject traversal/checksum errors even when leaks mask that status.
	zdb -e "${zdb_args[@]}" -bcc "$TESTPOOL" > "$check_log" 2>&1
	check_status=$?
	log_must cat "$check_log"
	((check_status == 0 || check_status == 2)) ||
	    log_fail "zdb exited $check_status"
	log_mustnot grep -q '^Error counts:' "$check_log"
	log_must grep -Eq 'bp count:[[:space:]]+[1-9]' "$check_log"
	log_must zdb -e "${zdb_args[@]}" -m --allocated-map "$TESTPOOL" \
	    > "$allocated_map"

	if [[ "$corruption" == "invalid-summary" ]]; then
		log_must grep -Eq \
		    '^injected invalid-summary: vdev=[0-9]+ metaslab=[0-9]+$' \
		    "$injection_log"
		log_mustnot grep -Eq 'metaslab .*free[[:space:]]+invalid$' \
		    "$allocated_map"
	else
		injection=$(<"$injection_log")
		vdev=${injection#*vdev=}
		vdev=${vdev%% *}
		if [[ "$corruption" == "passivate-repair" ]]; then
			log_must test "$vdev" -eq 1
		fi
		offset=${injection#*offset=}
		offset=${offset%% *}
		size=${injection#*size=}
		size=${size%% *}

		# The complete ambiguous range must remain allocated.
		log_must awk -v target_vdev="$vdev" \
		    -v target_start="$offset" -v target_size="$size" "
		    \$1 == \"vdev\" { current_vdev = \$2 }
		    current_vdev == target_vdev && \$1 == \"ALLOC:\" &&
		    \$2 <= target_start &&
		    \$2 + \$3 >= target_start + target_size { found = 1 }
		    END { exit !found }
		" "$allocated_map"
	fi

	log_must zpool import "${import_args[@]}" "$TESTPOOL"
	repaired_checksum=$(cksum "/$TESTPOOL/data")
	log_must test "$repaired_checksum" = "$checksum"
	log_must zpool destroy "$TESTPOOL"
	log_must set_tunable64 METASLAB_PRELOAD_ENABLED "$old_preload"
}

function run_unloadable_test
{
	typeset corruption=$1
	typeset dir="$tmpdir/$corruption"
	typeset flags
	typeset checksum
	typeset reopened_checksum
	typeset before
	typeset after
	typeset zdb_log="$dir/zdb"
	typeset leak_log="$dir/leak"
	typeset status_log="$dir/status"
	typeset injection_log="$dir/injection"
	typeset vdev="$dir/vdev"

	log_note "testing $corruption in a metaslab space map"
	log_must mkdir "$dir"
	log_must mkfile -n 512m "$vdev"
	log_must zpool create -o feature@log_spacemap=disabled \
	    -O compression=off "$TESTPOOL" "$vdev"
	log_must dd if=/dev/urandom of="/$TESTPOOL/data" bs=1M count=128
	checksum=$(cksum "/$TESTPOOL/data")
	log_must zpool sync "$TESTPOOL"
	log_must_busy zpool export "$TESTPOOL"

	before=$(kstat metaslab_stats.load_unloadable)
	log_must_retry "no suitable metaslab range" 5 \
	    inject_damage "$injection_log" -d "$dir" \
	    metaslab corrupt "$TESTPOOL" "$corruption" ||
	    log_fail "cannot inject $corruption"
	log_must set_tunable64 METASLAB_DEBUG_LOAD 1
	log_must zpool import -d "$dir" "$TESTPOOL"

	# Waiting succeeds after the invalid metaslab suspends initialization.
	log_must zpool initialize -w "$TESTPOOL" "$vdev"
	after=$(kstat metaslab_stats.load_unloadable)
	log_must test "$after" -gt "$before"
	log_must zpool status -i "$TESTPOOL" > "$status_log"
	log_must awk -v vdev="$vdev" "
	    index(\$0, vdev) && /suspended/ { found = 1 }
	    END { exit !found }
	" "$status_log"

	log_must zpool initialize -c "$TESTPOOL" "$vdev"
	log_must_busy zpool export "$TESTPOOL"
	zdb -e -p "$dir" -m --allocated-map "$TESTPOOL" > "$zdb_log" 2>&1
	log_must test $? -eq 3
	log_must grep -q "cannot load vdev" "$zdb_log"
	log_must grep -Eq 'metaslab .*free[[:space:]]+unavailable$' "$zdb_log"

	# An unavailable map must not turn leak recovery into a silent no-op.
	zhack -d "$dir" metaslab leak "$TESTPOOL" < "$zdb_log" \
	    > "$leak_log" 2>&1
	log_must test $? -eq 1
	log_must grep -q "cannot load vdev" "$leak_log"
	log_must grep -q "allocation map was not fully applied" "$leak_log"
	# High verbosity must not decode entries rejected by the checked load.
	for flags in mmmm Pmmmm; do
		zdb -e -p "$dir" -"$flags" "$TESTPOOL" \
		    > "$zdb_log.verbose" 2>&1
		log_must test $? -eq 3
		log_must grep -q "cannot load vdev" "$zdb_log.verbose"
		log_must grep -Eq 'metaslab .*free[[:space:]]+unavailable$' \
		    "$zdb_log.verbose"
		log_must grep -q "smp_length =" "$zdb_log.verbose"
	done
	# Raw dumps must detect malformed entries without loading metaslabs.
	if [[ $corruption != invalid-vdev ]]; then
		for flags in Lmmmm LPmmmm dddddd Pdddddd Ldddddd LPdddddd; do
			zdb -e -p "$dir" -"$flags" "$TESTPOOL" \
			    > "$zdb_log.raw" 2>&1
			log_must test $? -eq 3
			log_must grep -q "cannot decode space map" "$zdb_log.raw"
			log_must grep -q "smp_length =" "$zdb_log.raw"
			log_mustnot grep -q "cannot load vdev" "$zdb_log.raw"
			log_mustnot grep -q "In-memory histogram:" "$zdb_log.raw"
		done
	fi
	log_must zpool import -d "$dir" "$TESTPOOL"

	log_must set_tunable64 METASLAB_DEBUG_LOAD 0
	reopened_checksum=$(cksum "/$TESTPOOL/data")
	log_must test "$reopened_checksum" = "$checksum"
	log_must zpool destroy "$TESTPOOL"
}

log_assert "damaged metaslab space maps are repaired or quarantined safely"

old_debug_load=$(get_tunable METASLAB_DEBUG_LOAD)
old_preload=$(get_tunable METASLAB_PRELOAD_ENABLED)
old_keep_logs=$(get_tunable KEEP_LOG_SPACEMAPS_AT_EXPORT)
tmpdir=$(mktemp -d "$TEST_BASE_DIR/zhack_metaslab_repair.XXXXXX") ||
    log_fail "failed to create temporary directory"
log_onexit cleanup

run_allocated_map_test

for corruption in duplicate-free partial-free duplicate-alloc partial-alloc \
    chained invalid-summary log-duplicate-free; do
	run_repair_test "$corruption" enabled
done

# With no log space maps, initialization guarantees the damaged map loads.
run_repair_test partial-free disabled initialize
run_repair_test log-duplicate-free enabled initialize
run_repair_test log-duplicate-free enabled trim
# unload-repair also checks stale TRIM batches and in-progress condensation.
for corruption in pending-free unloaded-free unload-repair passivate-repair; do
	run_repair_test "$corruption" enabled zhack
	run_repair_test "$corruption" disabled zhack
done
run_unloadable_test invalid-entry
run_unloadable_test invalid-padding
run_unloadable_test invalid-vdev
run_unloadable_test truncated-entry

log_pass "metaslab damage was handled without changing referenced data"
