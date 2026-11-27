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

#
# Copyright (c) 2026 by Oxide Computer Company. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/removal/removal.kshlib

#
# DESCRIPTION:
# When a hot spare is in use and either the spare or the device that it
# spares is damaged, the split blocks of a removed vdev under them can be
# read from the other device, and a scrub repairs them.
#
# STRATEGY:
# 1. Create a pool, write a file, add a second top-level vdev and remove
#    the first one with 8K removal segments, so every 128K block of the
#    file is split into 16 segments on the second vdev.
# 2. Replace a leaf of the second vdev with a hot spare and wait for the
#    resilver.
# 3. Export the pool, overwrite the spare or the leaf, and import the pool.
# 4. Read the file, scrub and check for errors.  The overwritten device
#    must have checksum errors, and the other devices none.
# 5. Detach the device that wasn't overwritten and re-import the pool, so
#    that the repaired device holds the only copy of the file's segments.
#    Check the file, scrub and check for errors.
# 6. Do this for a single-disk vdev and for a child of a mirror.
# 7. With two top-level vdevs that have segments of the same blocks, and
#    with a second scrub in each check, which must find no checksum errors
#    on the overwritten devices:
#    - A mirror with a spared child, next to a single disk: overwrite both
#      children of the mirror, check, then overwrite the spare and check,
#      then overwrite the spare and the other child of the mirror and check.
#    - Two single disks that each have a hot spare in use: overwrite the
#      disk of one and the spare of the other, check, then the reverse.
#    - A 3-way mirror with hot spares attached to two children at once,
#      next to a single disk: detach the two children, check that the
#      spares have the data, then overwrite all but one spare, check, and
#      overwrite that one and check.
#
verify_runnable "global"

typeset VDEV_DIR=$TEST_BASE_DIR/removal_spare_damage
typeset VDEV_A=$VDEV_DIR/a
typeset VDEV_B1=$VDEV_DIR/b1
typeset VDEV_B2=$VDEV_DIR/b2
typeset VDEV_B3=$VDEV_DIR/b3
typeset VDEV_C=$VDEV_DIR/c
typeset VDEV_SPARE=$VDEV_DIR/spare
typeset VDEV_SPARE2=$VDEV_DIR/spare2

function cleanup
{
	log_must set_tunable32 REMOVE_MAX_SEGMENT $ORIG_REMOVE_MAX_SEGMENT
	log_must set_tunable64 METASLAB_ALIQUOT $ORIG_METASLAB_ALIQUOT
	log_must set_tunable32 SCAN_SUSPEND_PROGRESS $ORIG_SCAN_SUSPEND_PROGRESS
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
	rm -rf $VDEV_DIR
}

function setup_vdevs
{
	log_must mkdir -p $VDEV_DIR
	log_must truncate -s $MINVDEVSIZE $VDEV_A $VDEV_B1 $VDEV_B2 $VDEV_B3 \
	    $VDEV_C $VDEV_SPARE $VDEV_SPARE2
}

function destroy_split_pool
{
	destroy_pool $TESTPOOL
	rm -rf $VDEV_DIR
}

#
# Overwrites the given vdevs and checks the file and the pool, as
# check_split_pool does.  Then checks that its scrub repaired the vdevs.
#
function damage_and_check # vdev...
{
	typeset vdev

	damage_split_pool $VDEV_DIR "$@"
	check_split_pool

	#
	# With the error counts reset, another scrub finds no checksum errors
	# on the repaired vdevs.  The counts are reset by an export and
	# import: "zpool clear" also marks a spared device to be unspared,
	# and its hot spare is then detached when the scrub completes.
	#
	log_must zpool export $TESTPOOL
	log_must zpool import -d $VDEV_DIR $TESTPOOL
	log_must zpool scrub -w $TESTPOOL
	for vdev in "$@"; do
		log_must test \
		    "$(get_vdev_prop checksum_errors $TESTPOOL $vdev)" -eq 0
	done
}

#
# $1: "single" for a single-disk vdev, or "mirror" for a child of a mirror.
# $2: "spare" to damage the spare, or "spared" to damage the device that it
# spares.
#
function test_damage
{
	typeset layout=$1
	typeset damage=$2
	typeset spared=$VDEV_B1
	typeset damaged=$VDEV_SPARE
	typeset intact

	log_note "Damage the $damage device of a $layout vdev"

	setup_vdevs
	if [[ $layout == "mirror" ]]; then
		create_split_pool $VDEV_A mirror $VDEV_B1 $VDEV_B2
		spared=$VDEV_B2
	else
		create_split_pool $VDEV_A $VDEV_B1
	fi
	log_must zpool add $TESTPOOL spare $VDEV_SPARE
	log_must zpool replace $TESTPOOL $spared $VDEV_SPARE
	log_must zpool wait -t resilver $TESTPOOL

	intact=$spared
	if [[ $damage == "spared" ]]; then
		damaged=$spared
		intact=$VDEV_SPARE
	fi

	damage_split_pool $VDEV_DIR $damaged
	check_split_pool
	# Only the damaged device has checksum errors.
	log_must test \
	    "$(get_vdev_prop checksum_errors $TESTPOOL $damaged)" -gt 0
	log_must test \
	    "$(get_vdev_prop checksum_errors $TESTPOOL $intact)" -eq 0
	if [[ $layout == "mirror" ]]; then
		log_must test \
		    "$(get_vdev_prop checksum_errors $TESTPOOL $VDEV_B1)" -eq 0
	fi

	# Leave the repaired device with the only copy.
	log_must zpool detach $TESTPOOL $intact
	log_must zpool export $TESTPOOL
	log_must zpool import -d $VDEV_DIR $TESTPOOL
	if [[ $layout == "mirror" ]]; then
		log_must zpool offline $TESTPOOL $VDEV_B1
	fi
	check_split_pool

	destroy_split_pool
}

log_assert "Split blocks of a removed vdev can be read and repaired when" \
    "a hot spare or the device that it spares is damaged"

ORIG_REMOVE_MAX_SEGMENT=$(get_tunable REMOVE_MAX_SEGMENT)
ORIG_METASLAB_ALIQUOT=$(get_tunable METASLAB_ALIQUOT)
ORIG_SCAN_SUSPEND_PROGRESS=$(get_tunable SCAN_SUSPEND_PROGRESS)
log_onexit cleanup

test_damage single spare
test_damage single spared
test_damage mirror spare
test_damage mirror spared

SPLIT_POOL_SPREAD=1

log_note "Damage both children of a mirror while a hot spare has the data"
setup_vdevs
create_split_pool $VDEV_A $VDEV_C mirror $VDEV_B1 $VDEV_B2
log_must test "$SPLIT_SPREAD_BLOCKS" -gt 0
log_must zpool add $TESTPOOL spare $VDEV_SPARE
log_must zpool replace $TESTPOOL $VDEV_B2 $VDEV_SPARE
log_must zpool wait -t resilver,replace $TESTPOOL
damage_and_check $VDEV_B1 $VDEV_B2
damage_and_check $VDEV_SPARE
damage_and_check $VDEV_B1 $VDEV_SPARE
destroy_split_pool

log_note "Damage a spared disk and the hot spare of another disk"
setup_vdevs
create_split_pool $VDEV_A $VDEV_B1 $VDEV_C
log_must test "$SPLIT_SPREAD_BLOCKS" -gt 0
log_must zpool add $TESTPOOL spare $VDEV_SPARE $VDEV_SPARE2
log_must zpool replace $TESTPOOL $VDEV_B1 $VDEV_SPARE
log_must zpool replace $TESTPOOL $VDEV_C $VDEV_SPARE2
log_must zpool wait -t resilver,replace $TESTPOOL
damage_and_check $VDEV_B1 $VDEV_SPARE2
damage_and_check $VDEV_SPARE $VDEV_C
destroy_split_pool

log_note "Attach hot spares to two children of a 3-way mirror at once," \
    "then damage two of its three devices"
setup_vdevs
create_split_pool $VDEV_A $VDEV_C mirror $VDEV_B1 $VDEV_B2 $VDEV_B3
log_must test "$SPLIT_SPREAD_BLOCKS" -gt 0
log_must zpool add $TESTPOOL spare $VDEV_SPARE $VDEV_SPARE2
log_must set_tunable32 SCAN_SUSPEND_PROGRESS 1
log_must zpool replace $TESTPOOL $VDEV_B2 $VDEV_SPARE
log_must zpool replace $TESTPOOL $VDEV_B3 $VDEV_SPARE2
log_must set_tunable32 SCAN_SUSPEND_PROGRESS 0
log_must zpool wait -t resilver,replace $TESTPOOL
log_must zpool detach $TESTPOOL $VDEV_B2
log_must zpool detach $TESTPOOL $VDEV_B3
log_must zpool export $TESTPOOL
log_must zpool import -d $VDEV_DIR $TESTPOOL
log_must zpool offline $TESTPOOL $VDEV_B1
# Each spare must have all the data: neither is repaired from the other.
log_must zpool scrub -w $TESTPOOL
for vdev in $VDEV_SPARE $VDEV_SPARE2; do
	log_must test "$(get_vdev_prop checksum_errors $TESTPOOL $vdev)" -eq 0
done
check_split_pool
log_must zpool online $TESTPOOL $VDEV_B1
log_must zpool wait -t resilver $TESTPOOL
damage_and_check $VDEV_B1 $VDEV_SPARE
damage_and_check $VDEV_SPARE2
destroy_split_pool

SPLIT_POOL_SPREAD=""

log_pass "Split blocks of a removed vdev can be read and repaired when" \
    "a hot spare or the device that it spares is damaged"
