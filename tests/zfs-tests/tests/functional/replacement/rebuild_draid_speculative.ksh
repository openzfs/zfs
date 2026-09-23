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
. $STF_SUITE/tests/functional/replacement/replacement.cfg

#
# DESCRIPTION:
#	A dRAID rebuild row which uses all of its parity to reconstruct is
#	unverified. Its repair of a spare column must write only the device
#	being rebuilt, never the original device beside it.
#
# STRATEGY:
#	1. Create a single-parity dRAID with a hot spare, and write data.
#	2. Offline one child, so rows holding its data use all parity.
#	3. Flip a bit in every read of another child while sequentially
#	   rebuilding it onto the hot spare.
#	4. Detach the spare, clear the errors, online the offline child and
#	   scrub. The original child must have no checksum errors.
#

verify_runnable "global"

function cleanup
{
	zinject -c all >/dev/null 2>&1
	log_must set_tunable32 REBUILD_SCRUB_ENABLED $ORIG_SCRUB_ENABLED
	destroy_pool $TESTPOOL1
	rm -rf $workdir
}

log_assert "An unverified dRAID rebuild row repairs only the rebuilt device"
ORIG_SCRUB_ENABLED=$(get_tunable REBUILD_SCRUB_ENABLED)
workdir=$(mktemp -d $TEST_BASE_DIR/rebuild_draid_speculative.XXXXXX) ||
    log_fail "cannot create test directory"
log_onexit cleanup

log_must truncate -s 512M $workdir/disk-{0,1,2,3} $workdir/spare
log_must zpool create -f $TESTPOOL1 draid1:2d:4c:0s \
    $workdir/disk-{0,1,2,3} spare $workdir/spare
log_must zfs create -o compression=off -o primarycache=metadata \
    $TESTPOOL1/$TESTFS
mntpnt=$(get_prop mountpoint $TESTPOOL1/$TESTFS)
log_must dd if=/dev/urandom of=$mntpnt/file bs=1M count=64
sync_pool $TESTPOOL1

log_must set_tunable32 REBUILD_SCRUB_ENABLED 0
log_must zpool offline $TESTPOOL1 $workdir/disk-1
log_must zinject -d $workdir/disk-0 -e corrupt -T read -f 100 $TESTPOOL1
log_must zpool replace -s $TESTPOOL1 $workdir/disk-0 $workdir/spare
log_must zpool wait -t resilver $TESTPOOL1
log_must eval "zpool history -i $TESTPOOL1 | grep -q ' rebuild .* complete'"
flips=$(zinject | awk '/^ *[0-9]/ { print $NF }')
(( flips > 0 )) || log_fail "read corruption did not fire"
log_must zinject -c all

log_must zpool detach $TESTPOOL1 $workdir/spare
log_must zpool clear $TESTPOOL1
log_must zpool online $TESTPOOL1 $workdir/disk-1
log_must zpool wait -t resilver $TESTPOOL1
log_must zpool scrub -w $TESTPOOL1
cksum=$(zpool status -p $TESTPOOL1 |
    awk -v d=$workdir/disk-0 '$1 == d { print $5 }')
log_note "checksum errors on the original child: $cksum"
[[ "$cksum" == 0 ]] ||
    log_fail "the rebuild overwrote the original child with unverified data"

log_pass "An unverified dRAID rebuild row repaired only the rebuilt device"
