#!/bin/ksh
# SPDX-License-Identifier: CDDL-1.0
# ZTS supplies STF_SUITE, TESTPOOL, and DISKS.
# shellcheck disable=SC2154

#
# Verify that repairable metaslab damage is conservatively replaced with a
# canonical space map and malformed maps remain unavailable.
#

. "$STF_SUITE"/include/libtest.shlib

verify_runnable "global"

typeset old_debug_load
typeset old_keep_logs
typeset corruption
typeset tmpdir

function cleanup
{
	log_must set_tunable64 METASLAB_DEBUG_LOAD "$old_debug_load"
	log_must set_tunable64 KEEP_LOG_SPACEMAPS_AT_EXPORT "$old_keep_logs"
	if poolexists "$TESTPOOL"; then
		destroy_pool "$TESTPOOL"
	fi
	rm -rf "$tmpdir"
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

function run_repair_test
{
	typeset corruption=$1
	typeset log_spacemap=$2
	typeset checksum
	typeset repaired_checksum
	typeset injection
	typeset injection_log="$tmpdir/injection-$corruption-$log_spacemap"
	typeset allocated_map="$tmpdir/map-$corruption-$log_spacemap"
	typeset vdev
	typeset offset
	typeset size
	typeset -a vdevs

	read -r -A vdevs <<< "$DISKS"

	log_note "testing $corruption corruption with log_spacemap " \
	    "$log_spacemap"
	log_must zpool create -o feature@log_spacemap="$log_spacemap" \
	    "$TESTPOOL" "${vdevs[@]}"
	log_must dd if=/dev/urandom of="/$TESTPOOL/data" bs=1M count=16
	checksum=$(cksum "/$TESTPOOL/data")
	log_must zpool sync "$TESTPOOL"
	log_must dd if=/dev/urandom of="/$TESTPOOL/churn" bs=128k count=10
	log_must zpool sync "$TESTPOOL"

	if [[ "$log_spacemap" == "enabled" ]]; then
		log_must set_tunable64 KEEP_LOG_SPACEMAPS_AT_EXPORT 1
		log_must_busy zpool export "$TESTPOOL"
		log_must eval "zhack -o zfs_keep_log_spacemaps_at_export=1 " \
		    "metaslab corrupt $TESTPOOL $corruption > $injection_log"
	else
		log_must set_tunable64 KEEP_LOG_SPACEMAPS_AT_EXPORT 0
		log_must_busy zpool export "$TESTPOOL"
		log_must eval "zhack metaslab corrupt $TESTPOOL $corruption " \
		    "> $injection_log"
	fi

	# Invalid summaries are scheduled for repair during metaslab setup.
	if [[ "$corruption" == "invalid-summary" ]]; then
		log_must set_tunable64 METASLAB_DEBUG_LOAD 0
	else
		# Load every metaslab so import finds the injected overlap.
		log_must set_tunable64 METASLAB_DEBUG_LOAD 1
	fi
	log_must zpool import "$TESTPOOL"

	log_must wait_for_repair "$TESTPOOL"

	log_must set_tunable64 METASLAB_DEBUG_LOAD 0
	log_must set_tunable64 KEEP_LOG_SPACEMAPS_AT_EXPORT 0
	log_must_busy zpool export "$TESTPOOL"

	# zdb -b enables leak tracking, which strictly loads every space map.
	log_must zdb -e -b "$TESTPOOL"
	log_must eval "zdb -e -m --allocated-map $TESTPOOL > $allocated_map"

	if [[ "$corruption" != "invalid-summary" ]]; then
		injection=$(<"$injection_log")
		vdev=${injection#*vdev=}
		vdev=${vdev%% *}
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

	log_must zpool import "$TESTPOOL"
	repaired_checksum=$(cksum "/$TESTPOOL/data")
	log_must test "$repaired_checksum" = "$checksum"
	log_must zpool destroy "$TESTPOOL"
}

function run_unloadable_test
{
	typeset checksum
	typeset reopened_checksum
	typeset before
	typeset after
	typeset -a vdevs

	read -r -A vdevs <<< "$DISKS"

	log_note "testing an invalid metaslab space map entry"
	log_must zpool create -o feature@log_spacemap=disabled \
	    "$TESTPOOL" "${vdevs[@]}"
	log_must dd if=/dev/urandom of="/$TESTPOOL/data" bs=1M count=16
	checksum=$(cksum "/$TESTPOOL/data")
	log_must zpool sync "$TESTPOOL"
	log_must_busy zpool export "$TESTPOOL"

	before=$(kstat metaslab_stats.load_unloadable)
	log_must zhack metaslab corrupt "$TESTPOOL" invalid-entry
	log_must set_tunable64 METASLAB_DEBUG_LOAD 1
	log_must zpool import "$TESTPOOL"

	after=$(kstat metaslab_stats.load_unloadable)
	log_must test "$after" -gt "$before"

	# Waiting succeeds after the invalid metaslab suspends initialization.
	log_must zpool initialize -w "$TESTPOOL" "${vdevs[0]}"
	log_must eval "zpool status -i $TESTPOOL | grep -F ${vdevs[0]} | " \
	    "grep -q suspended"

	log_must zpool initialize -c "$TESTPOOL" "${vdevs[0]}"
	log_must_busy zpool export "$TESTPOOL"
	log_must zpool import "$TESTPOOL"

	log_must set_tunable64 METASLAB_DEBUG_LOAD 0
	reopened_checksum=$(cksum "/$TESTPOOL/data")
	log_must test "$reopened_checksum" = "$checksum"
	log_must zpool destroy "$TESTPOOL"
}

log_assert "damaged metaslab space maps are repaired or quarantined safely"

old_debug_load=$(get_tunable METASLAB_DEBUG_LOAD)
old_keep_logs=$(get_tunable KEEP_LOG_SPACEMAPS_AT_EXPORT)
tmpdir=$(mktemp -d "$TEST_BASE_DIR/zhack_metaslab_repair.XXXXXX") ||
    log_fail "failed to create temporary directory"
log_onexit cleanup

for corruption in duplicate-free partial-free duplicate-alloc partial-alloc \
    chained invalid-summary; do
	run_repair_test "$corruption" enabled
done

run_repair_test partial-free disabled
run_unloadable_test

log_pass "metaslab damage was handled without changing referenced data"
