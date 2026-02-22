#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
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
# Copyright (c) 2026 Klara, Inc.
#

. $STF_SUITE/include/libtest.shlib

verify_runnable "global"

#
# DESCRIPTION:
#
# Several zpool properties exist to expose metaslab allocation class space
# accounting.
#
# STRATEGY:
# 1. Create a pool with raidz (to validate expansion and deflation metrics)
# 2. For each allocation class:
#    - Add any required vdevs for this allocation class
#    - Prepare a dataset configured to utilize this allocation class
#    - Validate metrics reported by pool properties for allocation classes
# 3. For the whole pool, confirm that USABLE and USED report reasonable values
#

bs=128K
count=100

function writefile
{
	dd if=/dev/urandom of=$1 bs=$bs count=$count 2>/dev/null
}

nfiles=5

function writefiles
{
	typeset datadir=$1

	for i in {1..$nfiles}; do
		log_must writefile $TESTDIR/$datadir/file$i
	done
}

pool=$TESTPOOL1

function get_class_prop
{
	get_pool_prop "${1}_${2}" $pool
}

# Wrapper for test to give more context to logs
function check
{
	shift 2 # class prop (test args)
	test "$@"
}

function check_raidz_used # [mincap=1]
{
	typeset -i mincap=${1:-1}

	log_must check $class size $size -gt 0
	log_must check $class capacity $cap -ge $mincap -a $cap -le 100
	log_must check $class free $free -gt 0 -a $free -lt $size
	log_must check $class allocated $alloc -gt 0 -a $alloc -lt $size
	log_must check $class available $avail -gt 0 -a $avail -lt $free
	log_must check $class usable $usable -gt 0 -a $usable -lt $size
	log_must check $class used $used -gt 0 -a $used -lt $alloc
	log_must check $class expandsize $expandsz = "-"
	log_must check $class fragmentation $frag -lt 50
}

function check_raidz_unused
{
	log_must check $class size $size -gt 0
	log_must check $class capacity $cap -eq 0
	log_must check $class free $free -eq $size
	log_must check $class allocated $alloc -eq 0
	log_must check $class available $avail -gt 0 -a $avail -lt $free
	log_must check $class usable $usable -gt 0 -a $usable -lt $size
	log_must check $class used $used -eq 0
	log_must check $class expandsize $expandsz = "-"
	log_must check $class fragmentation $frag -eq 0
}

function check_nonraidz_used # [mincap=1]
{
	typeset -i mincap=${1:-1}

	log_must check $class size $size -gt 0
	log_must check $class capacity $cap -ge $mincap -a $cap -le 100
	log_must check $class free $free -gt 0 -a $free -lt $size
	log_must check $class allocated $alloc -gt 0 -a $alloc -lt $size
	log_must check $class available $avail -eq $free
	log_must check $class usable $usable -eq $size
	log_must check $class used $used -eq $alloc
	log_must check $class expandsize $expandsz = "-"
	log_must check $class fragmentation $frag -lt 50
}

function check_nonraidz_unused
{
	log_must check $class size $size -gt 0
	log_must check $class capacity $cap -eq 0
	log_must check $class free $free -eq $size
	log_must check $class allocated $alloc -eq 0
	log_must check $class available $avail -eq $free
	log_must check $class usable $usable -eq $size
	log_must check $class used $used -eq $alloc
	log_must check $class expandsize $expandsz = "-"
	log_must check $class fragmentation $frag -eq 0
}

# Log capacity tends to be >0% but <1% in these tests, so gets reported as 0.
# Let that slide and rely on allocated/free checks for sanity, rather than
# trying to tweak txg sync parameters to widen the race window.

function check_raidz_log_used
{
	check_raidz_used 0
}

function check_nonraidz_log_used
{
	check_nonraidz_used 0
}

function check_unavailable
{
	log_must check $class size $size -eq 0
	log_must check $class capacity $cap -eq 0
	log_must check $class free $free -eq 0
	log_must check $class allocated $alloc -eq 0
	log_must check $class available $avail -eq 0
	log_must check $class usable $usable -eq 0
	log_must check $class used $used -eq 0
	log_must check $class expandsize $expandsz = "-"
	log_must check $class fragmentation $frag = "-"
}

typeset -a classes=(
    "normal" "special" "dedup" "log" "embedded_log" "special_embedded_log"
)

normal_vdevs=$(seq -f $TEST_BASE_DIR/normal-vdev-%g 3)
normal_vdev_size=$((1 << 30)) # 1 GiB

special_vdevs=$(seq -f $TEST_BASE_DIR/special-vdev-%g 3)
special_vdev_size=$((512 << 20)) # 512 MiB

# Use a mirror for dedup to test expandsize.
dedup_vdevs=$(seq -f $TEST_BASE_DIR/dedup-vdev-%g 2)
dedup_vdev_size=$((256 << 20)) # 256 MiB

# The log class can't be raided or expanded, so we only need one vdev.
log_vdev="$TEST_BASE_DIR/log-vdev"
log_vdev_size=$((128 << 20)) # 128 MiB

embedded_slog_min_ms=$(get_tunable EMBEDDED_SLOG_MIN_MS)

function cleanup
{
	zpool destroy -f $pool
	rm -f $normal_vdevs $normal_expand_vdev
	rm -f $special_vdevs $special_expand_vdev
	rm -f $dedup_vdevs $dedup_expand_vdev
	rm -f $log_vdev
	set_tunable32 EMBEDDED_SLOG_MIN_MS $embedded_slog_min_ms
}
log_onexit cleanup

log_assert "zpool allocation class properties report metrics correctly"

# Lower the threshold for provisioning embedded log metaslabs on small vdevs.
log_must set_tunable32 EMBEDDED_SLOG_MIN_MS 8

log_must truncate -s $normal_vdev_size $normal_vdevs
log_must zpool create $pool \
    raidz $normal_vdevs
log_must zfs set mountpoint=$TESTDIR $pool

log_note "Normal Class"
log_must zfs create \
    $pool/normal
writefiles normal
sync_pool $pool
for class in "${classes[@]}"; do
	typeset -il size=$(get_class_prop $class size)
	typeset -i cap=$(get_class_prop $class capacity)
	typeset -il free=$(get_class_prop $class free)
	typeset -il alloc=$(get_class_prop $class allocated)
	typeset -il avail=$(get_class_prop $class available)
	typeset -il usable=$(get_class_prop $class usable)
	typeset -il used=$(get_class_prop $class used)
	typeset expandsz=$(get_class_prop $class expandsize)
	typeset frag=$(get_class_prop $class fragmentation)
	case $class in
	normal)
		check_raidz_used
		;;
	embedded_log)
		check_raidz_unused
		;;
	*)
		check_unavailable
		;;
	esac
done

log_note "Embedded Log Class"
log_must zfs create \
    -o sync=always \
    $pool/elog
writefiles elog
for class in "${classes[@]}"; do
	typeset -il size=$(get_class_prop $class size)
	typeset -i cap=$(get_class_prop $class capacity)
	typeset -il free=$(get_class_prop $class free)
	typeset -il alloc=$(get_class_prop $class allocated)
	typeset -il avail=$(get_class_prop $class available)
	typeset -il usable=$(get_class_prop $class usable)
	typeset -il used=$(get_class_prop $class used)
	typeset expandsz=$(get_class_prop $class expandsize)
	typeset frag=$(get_class_prop $class fragmentation)
	case $class in
	normal)
		check_raidz_used
		;;
	embedded_log)
		check_raidz_log_used
		;;
	*)
		check_unavailable
		;;
	esac
done

log_note "Special Class"
log_must truncate -s $special_vdev_size $special_vdevs
log_must zpool add $pool \
    special raidz $special_vdevs
log_must zfs create \
    -o recordsize=32K -o special_small_blocks=32K \
    $pool/special
writefiles special
sync_pool $pool
for class in "${classes[@]}"; do
	typeset -il size=$(get_class_prop $class size)
	typeset -i cap=$(get_class_prop $class capacity)
	typeset -il free=$(get_class_prop $class free)
	typeset -il alloc=$(get_class_prop $class allocated)
	typeset -il avail=$(get_class_prop $class available)
	typeset -il usable=$(get_class_prop $class usable)
	typeset -il used=$(get_class_prop $class used)
	typeset expandsz=$(get_class_prop $class expandsize)
	typeset frag=$(get_class_prop $class fragmentation)
	case $class in
	normal|special)
		check_raidz_used
		;;
	embedded_log)
		check_raidz_log_used
		;;
	special_embedded_log)
		check_raidz_unused
		;;
	*)
		check_unavailable
		;;
	esac
done

log_note "Special Embedded Log Class"
log_must zfs create \
    -o recordsize=32K -o special_small_blocks=32K \
    -o sync=always \
    $pool/selog
writefiles selog
for class in "${classes[@]}"; do
	typeset -il size=$(get_class_prop $class size)
	typeset -i cap=$(get_class_prop $class capacity)
	typeset -il free=$(get_class_prop $class free)
	typeset -il alloc=$(get_class_prop $class allocated)
	typeset -il avail=$(get_class_prop $class available)
	typeset -il usable=$(get_class_prop $class usable)
	typeset -il used=$(get_class_prop $class used)
	typeset expandsz=$(get_class_prop $class expandsize)
	typeset frag=$(get_class_prop $class fragmentation)
	case $class in
	normal|special)
		check_raidz_used
		;;
	embedded_log|special_embedded_log)
		check_raidz_log_used
		;;
	*)
		check_unavailable
		;;
	esac
done

log_note "Log Class"
log_must truncate -s $log_vdev_size $log_vdev
log_must zpool add $pool \
    log $log_vdev
log_must zfs create \
    -o sync=always \
    $pool/log
writefiles log
for class in "${classes[@]}"; do
	typeset -il size=$(get_class_prop $class size)
	typeset -i cap=$(get_class_prop $class capacity)
	typeset -il free=$(get_class_prop $class free)
	typeset -il alloc=$(get_class_prop $class allocated)
	typeset -il avail=$(get_class_prop $class available)
	typeset -il usable=$(get_class_prop $class usable)
	typeset -il used=$(get_class_prop $class used)
	typeset expandsz=$(get_class_prop $class expandsize)
	typeset frag=$(get_class_prop $class fragmentation)
	case $class in
	normal|special)
		check_raidz_used
		;;
	embedded_log|special_embedded_log)
		check_raidz_log_used
		;;
	log)
		check_noraidz_log_used
		;;
	*)
		check_unavailable
		;;
	esac
done

log_note "Dedup Class"
log_must truncate -s $dedup_vdev_size $dedup_vdevs
log_must zpool add $pool \
    dedup raidz $dedup_vdevs
log_must zfs create \
    -o dedup=on -o recordsize=4k \
    $pool/dedup
writefiles dedup
sync_pool $pool
for class in "${classes[@]}"; do
	typeset -il size=$(get_class_prop $class size)
	typeset -i cap=$(get_class_prop $class capacity)
	typeset -il free=$(get_class_prop $class free)
	typeset -il alloc=$(get_class_prop $class allocated)
	typeset -il avail=$(get_class_prop $class available)
	typeset -il usable=$(get_class_prop $class usable)
	typeset -il used=$(get_class_prop $class used)
	typeset expandsz=$(get_class_prop $class expandsize)
	typeset frag=$(get_class_prop $class fragmentation)
	case $class in
	normal|special)
		check_raidz_used
		;;
	embedded_log|special_embedded_log)
		check_raidz_log_used
		;;
	log)
		check_noraidz_log_used
		;;
	dedup)
		check_noraidz_used
		;;
	*)
		log_fail "unhandled class: $class"
		;;
	esac
done

# Expansion
typeset -il delta=$((32 << 20)) # 32 MiB
log_must truncate -s $((dedup_vdev_size + delta)) $dedup_vdevs
typeset -a vdevs=($dedup_vdevs)
log_must zpool online -e $pool ${vdevs[0]}
typeset -il size=$(get_class_prop dedup size)
typeset -il expandsz=$(get_class_prop dedup expandsize)
log_must test $expandsz -eq $((2 * delta))

# Pool-wide USABLE/USED
typeset -il size=$(get_pool_prop size $pool)
typeset -il usable=$(get_pool_prop usable $pool)
log_must test $usable -gt 0 -a $usable -lt $size
typeset -il alloc=$(get_pool_prop alloc $pool)
typeset -il used=$(get_pool_prop used $pool)
log_must test $used -gt 0 -a $used -lt $alloc

cleanup
