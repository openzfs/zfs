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

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/removal/removal.kshlib

#
# DESCRIPTION:
# A block that device removal split into many segments, whose copies on
# one child of each mirror are all damaged, is reconstructed from the other
# children by a scrub.  Only the damaged children are charged checksum
# errors.
#
# STRATEGY:
# 1. Create a pool, write a file, add a mirror and remove the first vdev
#    with 8K removal segments, so that every 128K block of the file is
#    split into 16 segments on the mirror.  Their copies have more
#    combinations than zfs_reconstruct_indirect_combinations_max.
# 2. Export the pool, overwrite one child of the mirror and import the pool.
# 3. Scrub, and check that no data error is reported and that only the
#    overwritten child has checksum errors.
# 4. Re-import the pool, and check the file and the pool.
# 5. Do this for each child of the mirror.
# 6. Do it with 1M blocks, which are split into 128 segments, so that the
#    number of combinations doesn't fit in 64 bits.
# 7. Do it with a three-way mirror whose first child is offline and whose
#    second child is overwritten.
# 8. Do it without checksums: the scrub can't tell which copy is good, so
#    check that the child that wasn't overwritten still has the file.
# 9. Do it with two mirrors that have segments of the same blocks, with
#    the same child of each overwritten, and with a different child of each.
#
verify_runnable "global"

typeset VDEV_DIR=$TEST_BASE_DIR/removal_with_split_damage
typeset VDEV_A=$VDEV_DIR/a
typeset VDEV_B1=$VDEV_DIR/b1
typeset VDEV_B2=$VDEV_DIR/b2
typeset VDEV_B3=$VDEV_DIR/b3
typeset VDEV_C1=$VDEV_DIR/c1
typeset VDEV_C2=$VDEV_DIR/c2

function cleanup
{
	log_must set_tunable32 REMOVE_MAX_SEGMENT $ORIG_REMOVE_MAX_SEGMENT
	log_must set_tunable64 METASLAB_ALIQUOT $ORIG_METASLAB_ALIQUOT
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
	rm -rf $VDEV_DIR
}

function setup_vdevs
{
	log_must mkdir -p $VDEV_DIR
	log_must truncate -s $MINVDEVSIZE $VDEV_A $VDEV_B1 $VDEV_B2 $VDEV_B3 \
	    $VDEV_C1 $VDEV_C2
}

#
# Overwrite the vdevs given by the arguments, which are among $LEAVES, and
# check that a scrub repairs them from the other vdevs.
#
function test_damage # vdev...
{
	typeset vdev errors

	damage_split_pool $VDEV_DIR "$@"

	log_must zpool scrub -w $TESTPOOL
	log_must zpool status -v $TESTPOOL
	log_must check_pool_status $TESTPOOL "errors" "No known data errors"
	for vdev in $LEAVES; do
		errors=$(get_vdev_prop checksum_errors $TESTPOOL $vdev)
		if [[ " $* " == *" $vdev "* ]]; then
			log_must test "$errors" -gt 0
		else
			log_must test "$errors" -eq 0
		fi
	done

	# The first scrub repaired every copy, so the next one finds nothing.
	log_must zpool export $TESTPOOL
	log_must zpool import -d $VDEV_DIR $TESTPOOL
	check_split_pool
	for vdev in $LEAVES; do
		errors=$(get_vdev_prop checksum_errors $TESTPOOL $vdev)
		log_must test "$errors" -eq 0
	done

	destroy_pool $TESTPOOL
	rm -rf $VDEV_DIR
}

#
# Without checksums, a scrub can't tell which copy of a segment is good, so
# it must not overwrite either.  Overwrite the vdev $1, scrub, and check that
# the other child of the mirror still has the file.
#
function test_damage_nocksum # vdev
{
	damage_split_pool $VDEV_DIR $1

	log_must zpool scrub -w $TESTPOOL
	log_must zpool status -v $TESTPOOL
	log_must zpool detach $TESTPOOL $1
	log_must zpool export $TESTPOOL
	log_must zpool import -d $VDEV_DIR $TESTPOOL
	check_split_pool

	destroy_pool $TESTPOOL
	rm -rf $VDEV_DIR
}

log_assert "Split blocks of a removed vdev are reconstructed when one" \
    "child of each mirror is damaged"

ORIG_REMOVE_MAX_SEGMENT=$(get_tunable REMOVE_MAX_SEGMENT)
ORIG_METASLAB_ALIQUOT=$(get_tunable METASLAB_ALIQUOT)
log_onexit cleanup

typeset LEAVES="$VDEV_B1 $VDEV_B2"
for damaged in $LEAVES; do
	log_note "One mirror, $damaged overwritten"
	setup_vdevs
	create_split_pool $VDEV_A mirror $VDEV_B1 $VDEV_B2
	test_damage $damaged
done

#
# Each 1M block has at least 128 segments with two copies each, so the number
# of combinations (2^128) doesn't fit in 64 bits.  If the count wrapped around
# to zero, the scrub would try every combination of a block whose first copy
# is bad and never finish, so this case would be killed by the test runner's
# timeout instead of failing.
#
SPLIT_POOL_RECORDSIZE=1M
for damaged in $LEAVES; do
	log_note "One mirror, 1M blocks, $damaged overwritten"
	setup_vdevs
	create_split_pool $VDEV_A mirror $VDEV_B1 $VDEV_B2
	log_must test "$SPLIT_MIN_SEGMENTS" -ge 64
	test_damage $damaged
done
SPLIT_POOL_RECORDSIZE=""

#
# The copies of a segment are on the mirror's children in order.  The first
# one can't be read, so the scrub must skip it when it takes every segment
# from the same child.
#
log_note "One three-way mirror, $VDEV_B1 offline, $VDEV_B2 overwritten"
LEAVES="$VDEV_B1 $VDEV_B2 $VDEV_B3"
setup_vdevs
create_split_pool $VDEV_A mirror $VDEV_B1 $VDEV_B2 $VDEV_B3
log_must zpool offline $TESTPOOL $VDEV_B1
test_damage $VDEV_B2

for damaged in $VDEV_B1 $VDEV_B2; do
	log_note "One mirror without checksums, $damaged overwritten"
	setup_vdevs
	SPLIT_POOL_OPTS="-O checksum=off"
	create_split_pool $VDEV_A mirror $VDEV_B1 $VDEV_B2
	SPLIT_POOL_OPTS=""
	test_damage_nocksum $damaged
done

LEAVES="$VDEV_B1 $VDEV_B2 $VDEV_C1 $VDEV_C2"
SPLIT_POOL_SPREAD=1
for damaged in $VDEV_C1 $VDEV_C2; do
	log_note "Two mirrors, $VDEV_B1 and $damaged overwritten"
	setup_vdevs
	create_split_pool $VDEV_A mirror $VDEV_B1 $VDEV_B2 \
	    mirror $VDEV_C1 $VDEV_C2
	log_must test "$SPLIT_SPREAD_BLOCKS" -gt 0
	test_damage $VDEV_B1 $damaged
done
SPLIT_POOL_SPREAD=""

log_pass "Split blocks of a removed vdev are reconstructed when one" \
    "child of each mirror is damaged"
