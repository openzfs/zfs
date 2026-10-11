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
. $STF_SUITE/tests/functional/direct/dio.kshlib

#
# DESCRIPTION:
# A Direct I/O write of the data a block already holds is a nopwrite.
#
# STRATEGY:
# 1. Create a dataset with nopwrite enabled (sha256, compression).
# 2. Write a file and sync it.
# 3. Rewrite the file with the same data with Direct I/O and sync.
# 4. Verify the writes were Direct I/O, the block pointers did not
#    change, and the file data.
#

verify_runnable "global"

FS=$TESTPOOL/dio_nopwrite
BLK=$TEST_BASE_DIR/dio_nopwrite_blk

function cleanup
{
	destroy_dataset $FS
	rm -f $BLK
}

function l0_bps # dataset object
{
	zdb -ddddd $1 $2 | awk '$2 == "L0" { print $1, $3, $6 }'
}

log_assert "A Direct I/O write of the same data is a nopwrite"
log_onexit cleanup

log_must zfs create -o checksum=sha256 -o compression=lz4 \
    -o recordsize=128k -o direct=standard $FS
mntpnt=$(get_prop mountpoint $FS)
file=$mntpnt/file

log_must stride_dd -i /dev/urandom -o $BLK -b 131072 -c 4
log_must cp $BLK $file
sync_pool $TESTPOOL

obj=$(get_objnum $file)
before=$(l0_bps $FS $obj)
log_note "L0 block pointers before: $before"
[[ $(echo "$before" | wc -l) -eq 4 ]] || log_fail "Expected 4 L0 blocks"

prev_dio_wr=$(kstat_pool $TESTPOOL iostats.direct_write_count)
log_must stride_dd -i $BLK -o $file -b 131072 -c 4 -D
curr_dio_wr=$(kstat_pool $TESTPOOL iostats.direct_write_count)
if [[ $((curr_dio_wr - prev_dio_wr)) -lt 4 ]]; then
	log_fail "Direct writes $((curr_dio_wr - prev_dio_wr)) of 4"
fi
sync_pool $TESTPOOL

after=$(l0_bps $FS $obj)
log_note "L0 block pointers after: $after"
[[ "$before" == "$after" ]] || log_fail "The Direct I/O writes were not nopwrites"

log_must cmp $BLK $file

log_pass "A Direct I/O write of the same data is a nopwrite"
