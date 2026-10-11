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

. $STF_SUITE/tests/functional/cli_root/zpool_import/zpool_import.kshlib

#
# DESCRIPTION:
#	A dry run of a rewind (zpool import -nFX -T <txg>) must not write to
#	the pool, even when the txg it loads is so old that the space of some
#	of its blocks has been reused by later txgs.
#
# STRATEGY:
#	1. Create a pool on a small file vdev and sync it many times, so the
#	   space of the earlier txgs' blocks (the metadata ditto copies in
#	   particular) is freed and reused.
#	2. Export the pool and save a copy of the vdev.
#	3. Dry run a rewind to every older txg that still has an uberblock.
#	4. Verify that the vdev was not modified outside its labels, and
#	   that the pool still imports and scrubs clean at its newest txg.
#
# NOTES:
#	When an old txg's block has a copy whose space was reused and another
#	copy that is still intact, the read of the intact copy used to
#	"repair" the reused one with the old contents, overwriting live
#	blocks of the newest txg.
#
#	The labels are left out of the comparison: the dry runs only read
#	them, but this test is about the allocatable area.
#

verify_runnable "global"

typeset vdev=$DEVICE_DIR/dryrun0
typeset saved=$DEVICE_DIR/dryrun0.saved

function custom_cleanup
{
	cleanup
	rm -f $vdev $saved
}

log_onexit custom_cleanup

log_assert "A dry run of a rewind does not write to the pool."

log_must truncate -s 64m $vdev
log_must zpool create -O compression=off $TESTPOOL1 $vdev

#
# Every sync rewrites the MOS and the dataset's metadata and frees the
# previous copies, whose space is reused a few txgs later.
#
typeset -i i
for ((i = 0; i < 150; i++)); do
	log_must dd if=/dev/urandom of=/$TESTPOOL1/f$i bs=4k count=4 \
	    status=none
	log_must zpool sync $TESTPOOL1
done
log_must zpool export $TESTPOOL1
log_must cp $vdev $saved

#
# Newest first, as a rewind would visit them.  The newest txg itself is
# not a rewind.
#
typeset txgs=$(zdb -lu $vdev | awk '$1 == "txg" && $3 > 4 { print $3 }' |
    sort -nru | tail -n +2)
[[ -n "$txgs" ]] || log_fail "No older uberblocks found"
typeset txg
for txg in $txgs; do
	zpool import -d $DEVICE_DIR -nFX -T $txg $TESTPOOL1 >/dev/null 2>&1
done
log_note "Dry runs of $(echo $txgs | wc -w) txgs"
poolexists $TESTPOOL1 && log_fail "A dry run imported $TESTPOOL1"

#
# Compare everything between the front labels and boot area (4 MiB) and
# the back labels (512 KiB).
#
typeset -i blocks=$(( (64 * 1024 - 4 * 1024 - 512) / 512 ))
typeset before=$(dd if=$saved bs=512k skip=8 count=$blocks status=none |
    xxh128digest)
typeset after=$(dd if=$vdev bs=512k skip=8 count=$blocks status=none |
    xxh128digest)
[[ -n "$before" ]] || log_fail "No digest of the saved vdev"
if [[ "$before" != "$after" ]]; then
	cmp -l $saved $vdev | awk '$1 > 4194304 && $1 <= 66584576 {
	    n[int(($1 - 1) / 512)] = 1 } END { print length(n) }' |
	    read sectors
	log_fail "The dry runs changed $sectors sectors of the vdev"
fi

log_must zpool import -d $DEVICE_DIR $TESTPOOL1
log_must zpool scrub -w $TESTPOOL1
log_must check_pool_healthy $TESTPOOL1

log_pass "A dry run of a rewind does not write to the pool."
