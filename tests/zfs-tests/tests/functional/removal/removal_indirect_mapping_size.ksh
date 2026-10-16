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

# DESCRIPTION:
#	zdb -b reports, for each indirect vdev, the number of segments of
#	its mapping and the memory the mapping takes. With -P that size is
#	an exact number of bytes.
#
# STRATEGY:
#	1. Create a pool on a single disk and write a file to it.
#	2. Add a second disk and remove the first, leaving an indirect vdev
#	   whose mapping covers the file.
#	3. Run zdb -bPL and check that the size in memory is that of the
#	   reported number of mapping entries.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/removal/removal.kshlib

verify_runnable "global"

log_assert "zdb -bP reports the size of an indirect vdev's mapping"

set -A disks $DISKS
if (( ${#disks[@]} < 2 )); then
	log_unsupported "needs at least two disks"
fi

zdbout=$TEST_BASE_DIR/zdbout.$$

function cleanup
{
	if poolexists $TESTPOOL ; then
		destroy_pool $TESTPOOL
	fi
	rm -f $zdbout
}

log_onexit cleanup

log_must zpool create -f -O compression=off $TESTPOOL ${disks[0]}
log_must zfs create $TESTPOOL/$TESTFS
typeset mountpoint
mountpoint=$(get_prop mountpoint $TESTPOOL/$TESTFS)
log_must dd if=/dev/urandom of=$mountpoint/file1 bs=128k count=8
sync_pool $TESTPOOL

log_must zpool add $TESTPOOL ${disks[1]}
log_must zpool remove $TESTPOOL ${disks[0]}
log_must wait_for_removal $TESTPOOL
sync_pool $TESTPOOL

log_must eval "zdb -bPL $TESTPOOL > $zdbout"
line=$(grep "indirect vdev id 0 has" $zdbout)
log_note "$line"
segments=$(echo "$line" | awk '{ print $6 }')
size=$(echo "$line" | awk '{ print substr($8, 2) }')
log_must test "$segments" -gt 0

# Each entry is a vdev_indirect_mapping_entry_phys_t of 24 bytes.
log_must test "$size" -eq $((segments * 24))

log_pass "zdb -bP reports the size of an indirect vdev's mapping"
