#! /bin/ksh -p
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
# DESCRIPTION:
#	A pool whose indirect vdev mapping or births object cannot be read
#	fails to import with an error, instead of panicking on the read,
#	both once a removal has finished and while one is in progress.
#
# STRATEGY:
#	1. Create a pool on a file vdev, write some files, add a second
#	   file vdev and remove the first, leaving an indirect vdev.
#	2. Export the pool and keep a copy of the remaining vdev.
#	3. Overwrite every copy of the mapping object's data block.
#	4. Check that zdb fails to load the pool in spa_remove_init(), and
#	   that zpool import fails.
#	5. Put the vdev back and do the same with the births object.
#	6. Put the vdev back and check that the pool imports, that the
#	   files read back unchanged and that a scrub finds no error.
#	7. Do it all again with the removal suspended while in progress.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/removal/removal.kshlib

verify_runnable "global"

VDIR=$TEST_BASE_DIR/removal_unreadable_mapping.$$
VDEV_A=$VDIR/a
VDEV_B=$VDIR/b
SAVED_B=$TEST_BASE_DIR/removal_unreadable_mapping.b.$$
ZDBOUT=$TEST_BASE_DIR/removal_unreadable_mapping.zdb.$$
CKSUMS=$TEST_BASE_DIR/removal_unreadable_mapping.cksum.$$

function cleanup
{
	set_tunable32 REMOVAL_SUSPEND_PROGRESS 0
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
	rm -rf $VDIR $SAVED_B $ZDBOUT $CKSUMS
}

#
# The MOS object number stored under com.delphix:<key> in the
# configuration of the indirect or removing vdev.
#
function indirect_object # key
{
	zdb -e -p $VDIR -C $TESTPOOL |
	    awk -v key="com.delphix:$1:" '$1 == key { print $2; exit }'
}

#
# Overwrite every copy of every level 0 block of MOS object <object>.
# All of them are on the only allocatable vdev, vdev 1. A DVA offset
# leaves out the 4M of labels and boot block at the start of the vdev.
#
function damage_object # object
{
	typeset obj=$1
	typeset vdev offset asize
	typeset -i n=0

	log_must eval "zdb -e -p $VDIR -dddddd $TESTPOOL $obj > $ZDBOUT"
	awk '
	    /^$/ { looking = 0 }
	    looking && $2 == "L0" {
		for (f = 3; split($f, dva, ":") == 3; f++)
			print dva[1], dva[2], dva[3]
	    }
	    /^Indirect blocks:/ { looking = 1 }
	' $ZDBOUT | while read vdev offset asize; do
		[[ $vdev == 1 ]] ||
		    log_fail "object $obj has a block on vdev $vdev"
		log_must dd if=/dev/urandom of=$VDEV_B bs=512 conv=notrunc \
		    seek=$(( (0x$offset + (4 << 20)) / 512 )) \
		    count=$(( 0x$asize / 512 ))
		(( n += 1 ))
	done
	(( n > 0 )) || log_fail "object $obj has no block to damage"
	log_note "overwrote $n block copies of object $obj"
}

#
# zdb loads the pool through the same code as an import, in userspace,
# so it runs first: without the fix it aborts there, and the kernel is
# never asked to import the damaged pool. Its debug messages must show
# that the load failed where the damage is, not for another reason.
#
function check_unimportable # what
{
	typeset -i rc

	zdb -G -e -p $VDIR -d $TESTPOOL > $ZDBOUT 2>&1
	rc=$?
	if (( rc == 0 || rc > 127 )) ||
	    ! grep -q "spa_remove_init failed" $ZDBOUT; then
		cat $ZDBOUT
		log_fail "zdb exited $rc, not failing in spa_remove_init," \
		    "on a pool with an unreadable $1"
	fi
	log_note "zdb exited $rc: $(grep -m 1 "spa_remove_init failed" $ZDBOUT)"

	log_mustnot zpool import -d $VDIR $TESTPOOL
	log_mustnot poolexists $TESTPOOL
}

#
# Count the removal thread's starts on a metaslab with data to copy.
#
function copy_starts
{
	kstat dbgmsg | grep -c "copying [1-9][0-9]* segments for metaslab"
}

#
# Build the pool, damage and check its mapping and births objects, then
# check that it imports once restored. With <in_progress> true, the
# removal is suspended once it has started copying, and the pool is
# exported in the middle of it: the removal thread then copies one more
# chunk before it stops, so the mapping and births have a block on disk.
#
function test_removal # in_progress
{
	typeset in_progress=$1
	typeset what obj
	typeset -i before i

	log_must mkdir $VDIR
	log_must truncate -s $MINVDEVSIZE $VDEV_A $VDEV_B
	log_must zpool create -f -o cachefile=none -O compression=off \
	    $TESTPOOL $VDEV_A
	mntpnt=$(get_prop mountpoint $TESTPOOL)
	for i in {1..16}; do
		log_must dd if=/dev/urandom of=$mntpnt/file$i bs=64k count=4
	done
	log_must eval "cksum $mntpnt/file* > $CKSUMS"
	sync_pool $TESTPOOL

	log_must zpool add -f $TESTPOOL $VDEV_B
	if $in_progress; then
		before=$(copy_starts)
		log_must set_tunable32 REMOVAL_SUSPEND_PROGRESS 1
		log_must zpool remove $TESTPOOL $VDEV_A
		for (( i = 0; i < 300; i++ )); do
			(( $(copy_starts) > before )) && break
			sleep 0.1
		done
		(( i < 300 )) || log_fail "the removal did not start copying"
		log_must is_pool_removing $TESTPOOL
		log_must zpool export $TESTPOOL
	else
		log_must zpool remove $TESTPOOL $VDEV_A
		log_must wait_for_removal $TESTPOOL
		log_must zpool export $TESTPOOL
		log_must rm $VDEV_A
	fi
	log_must cp $VDEV_B $SAVED_B

	for what in indirect_object indirect_births; do
		obj=$(indirect_object $what)
		[[ -n $obj ]] || log_fail "no $what in the configuration"
		damage_object $obj
		check_unimportable "$what (removal in progress: $in_progress)"
		log_must cp $SAVED_B $VDEV_B
	done

	log_must zpool import -d $VDIR $TESTPOOL
	if $in_progress; then
		log_must set_tunable32 REMOVAL_SUSPEND_PROGRESS 0
		log_must wait_for_removal $TESTPOOL
	fi
	log_must eval "cksum $mntpnt/file* | cmp - $CKSUMS"
	log_must zpool scrub -w $TESTPOOL
	log_must check_pool_status $TESTPOOL "errors" "No known data errors"
	log_must destroy_pool $TESTPOOL
	log_must rm -rf $VDIR $SAVED_B $CKSUMS
}

log_assert "Importing a pool whose indirect mapping or births object" \
    "cannot be read fails with an error"
log_onexit cleanup

test_removal false
test_removal true

log_pass "Importing a pool whose indirect mapping or births object" \
    "cannot be read fails with an error"
