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
# A sequential rebuild ("zpool attach -s") that can't read the old device
# must neither write the new device's stale contents over it nor empty
# the new device's DTL: the old device keeps its data, can't be detached,
# and a scrub copies the data once it reads again.
#
# STRATEGY:
# 1. Create a pool of one data device a and a special device (so the
#    metadata stays readable) and fill it with files.
# 2. With the scrub that normally follows a rebuild disabled, make every
#    read of a fail and attach b with "zpool attach -s".
# 3. Stop the read errors.  Every file must be intact (read from a, the
#    only device that had them).
# 4. "zpool detach" of a must fail: b doesn't hold the data.
# 5. Scrub: the data is copied to b.  Detach a; every file is intact on
#    b alone and the pool is healthy.
#

verify_runnable "global"

DIR=$TEST_BASE_DIR/rebuild_unreadable
NFILES=20

function cleanup
{
	restore_tunable REBUILD_SCRUB_ENABLED
	zinject -c all >/dev/null 2>&1
	poolexists $TESTPOOL1 && destroy_pool $TESTPOOL1
	rm -rf $DIR
}

function check_files # mode
{
	typeset i

	for i in $(seq $NFILES); do
		[[ "$(xxh128digest $mnt/f$i)" == "${sums[$i]}" ]] ||
		    log_fail "$1: $mnt/f$i is damaged"
	done
}

log_assert "A sequential rebuild that can't read the old device keeps its" \
    "data and leaves the new device's DTL in place"
log_onexit cleanup
log_must save_tunable REBUILD_SCRUB_ENABLED

rm -rf $DIR
log_must mkdir -p $DIR
log_must truncate -s 512M $DIR/a $DIR/b $DIR/s
log_must zpool create -f -o ashift=12 -O compression=off \
    -O primarycache=metadata $TESTPOOL1 $DIR/a special $DIR/s
mnt=$(get_prop mountpoint $TESTPOOL1)
set -A sums
for i in $(seq $NFILES); do
	log_must dd if=/dev/urandom of=$mnt/f$i bs=128k count=8 status=none
	sums[$i]=$(xxh128digest $mnt/f$i)
done
log_must zpool export $TESTPOOL1
log_must zpool import -d $DIR $TESTPOOL1

log_must set_tunable32 REBUILD_SCRUB_ENABLED 0
log_must zinject -d $DIR/a -e io -T read -f 100 $TESTPOOL1
log_must zpool attach -s $TESTPOOL1 $DIR/a $DIR/b
log_must zpool wait -t resilver $TESTPOOL1
zpool status -v $TESTPOOL1
log_must zinject -c all

# The rebuild must not have replaced a's data with b's stale contents.
check_files "after the rebuild"

# The rebuild couldn't copy the data, so b still lacks it and a must stay.
log_mustnot zpool detach $TESTPOOL1 $DIR/a

# Now that a reads again, a scrub copies the data to b.
log_must zpool scrub -w $TESTPOOL1
log_must zpool detach $TESTPOOL1 $DIR/a
log_must zpool export $TESTPOOL1
log_must zpool import -d $DIR $TESTPOOL1
check_files "on b alone"
log_must zpool scrub -w $TESTPOOL1
zpool status -v $TESTPOOL1
log_must check_pool_status $TESTPOOL1 "errors" "No known data errors"

log_pass "A sequential rebuild that can't read the old device keeps its" \
    "data and leaves the new device's DTL in place"
