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
# Description:
# Verify that a scrub repairs the copies of a gang block other than the
# first: the second copy of a gang header, and the second copy of a gang
# child.
#
# Strategy:
# 1. Create a pool on a file vdev and force 128K blocks to gang.
# 2. Write a block with copies=1, whose gang header has two copies, and a
#    block with copies=2, whose gang children have two copies each.
# 3. Export the pool and overwrite the second copy of the first block's
#    gang header and the second copy of the second block's first gang
#    child.
# 4. Import the pool and scrub it.
# 5. Verify that the scrub restored both copies.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/gang_blocks/gang_blocks.kshlib

verify_runnable "global"

typeset vdev=$TEST_BASE_DIR/gang_blocks_scrub_copies
typeset -i sector_size=4096

function cleanup2
{
	cleanup
	rm -f $vdev $vdev.*
}

#
# Print the sector of the vdev at which a copy, given as a DVA in zdb's
# vdev:offset:asize form, starts.  The front labels and the boot block
# take the first 4 MiB of the vdev.
#
function dva_sector # dva
{
	typeset offset=$(echo $1 | awk -F: '{print $2}')

	echo $(( (0x$offset + (4 << 20)) / sector_size ))
}

#
# Save a sector, then overwrite it with random data.
#
function damage_sector # sector
{
	log_must dd if=$vdev of=$vdev.$1 bs=$sector_size skip=$1 count=1
	log_must dd if=/dev/urandom of=$vdev bs=$sector_size seek=$1 \
	    count=1 conv=notrunc
}

#
# Check that a sector holds what damage_sector saved again.
#
function check_sector # sector description
{
	log_must dd if=$vdev of=$vdev.now bs=$sector_size skip=$1 count=1
	if cmp -s $vdev.$1 $vdev.now; then
		log_note "The scrub repaired $2"
		return 0
	fi
	log_note "The scrub did not repair $2"
	return 1
}

log_assert "A scrub repairs every copy of a gang block."

preamble
log_onexit cleanup2

log_must truncate -s 256m $vdev
log_must zpool create -o ashift=12 $TESTPOOL $vdev
log_must zfs create $TESTPOOL/$TESTFS
log_must zfs create -o copies=2 $TESTPOOL/$TESTFS1

set_tunable64 METASLAB_FORCE_GANGING 102400
set_tunable32 METASLAB_FORCE_GANGING_PCT 100
for fs in $TESTFS $TESTFS1; do
	mntpnt=$(get_prop mountpoint $TESTPOOL/$fs)
	log_must dd if=/dev/urandom of=$mntpnt/file bs=128k count=1
done
sync_pool $TESTPOOL
restore_tunable METASLAB_FORCE_GANGING
restore_tunable METASLAB_FORCE_GANGING_PCT

# The second copy of the gang header of the copies=1 block.
hdr=$(get_first_block $TESTPOOL/$TESTFS file | awk '{print $4}')
check_is_gang_dva $hdr

# The second copy of the first gang child of the copies=2 block.
gang=$(get_first_block_dva $TESTPOOL/$TESTFS1 file)
bp=$(read_gang_header $TESTPOOL $gang 1000 | grep -v HOLE | head -n 1)
check_not_gang_bp "$bp"
child=$(echo "$bp" | sed -n 's/.*DVA\[1\]=<\([^>]*\)>.*/\1/p')
[[ -n "$child" ]] || log_fail "No second copy of a gang child: \"$bp\""

hdr_sector=$(dva_sector $hdr)
child_sector=$(dva_sector $child)
log_note "Damaging gang header copy $hdr and gang child copy $child"

log_must zpool export $TESTPOOL
damage_sector $hdr_sector
damage_sector $child_sector
log_must zpool import -d $vdev $TESTPOOL
log_must zpool scrub -w $TESTPOOL
log_must zpool status -v $TESTPOOL
log_must zpool export $TESTPOOL

typeset -i missed=0
check_sector $hdr_sector "a gang header's second copy" || (( missed += 1 ))
check_sector $child_sector "a gang child's second copy" || (( missed += 1 ))
(( missed == 0 )) || log_fail "The scrub left $missed damaged copies"

log_pass "A scrub repairs every copy of a gang block."
