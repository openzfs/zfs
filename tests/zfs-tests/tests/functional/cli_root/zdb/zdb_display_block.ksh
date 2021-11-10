#!/bin/ksh

#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# http://www.illumos.org/license/CDDL.
#

#
# Copyright (c) 2019 by Datto, Inc. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# Description:
# zdb -R pool <DVA>:b will display the block
#
# Strategy:
# 1. Create a pool, set compression to lzjb
# 2. Write some identifiable data to a file
# 3. Run zdb -ddddddbbbbbb against the file
# 4. Record the DVA of the first L1 block;
#    record the first L0 block display; and
#    record the 2nd L0 block display.
# 5. Run zdb -R with :bd displays first L0
# 6. Run zdb -R with :b80d displays 2nd L0
# 7. Run zdb -R with :db80 displays 2nd L0
# 8. Run zdb -R with :id flag displays indirect block
#     (similar to zdb -ddddddbbbbbb output)
# 9. Run zdb -R with :id flag and .0 vdev
#


function cleanup
{
	datasetexists $TESTPOOL && destroy_pool $TESTPOOL
}

log_assert "Verify zdb -R :b flag (block display) works as expected"
log_onexit cleanup
init_data=$TESTDIR/file1
write_count=256
blksize=4096

# only read 256 128 byte block pointers in L1 (:i flag)
# 256 x 128 = 32k / 0x8000
l1_read_size="8000"

verify_runnable "global"
verify_disk_count "$DISKS" 2

default_mirror_setup_noexit $DISKS
log_must zfs set recordsize=$blksize $TESTPOOL/$TESTFS
log_must zfs set compression=lzjb $TESTPOOL/$TESTFS

file_write -d R -o create -w -f $init_data -b $blksize -c $write_count
sync_pool $TESTPOOL true

# get object number of file
listing=$(ls -i $init_data)
set -A array $listing
obj=${array[0]}
log_note "file $init_data has object number $obj"

output=$(zdb -ddddddbbbbbb $TESTPOOL/$TESTFS $obj 2> /dev/null \
    |grep -m 1 "L1  DVA" |head -n1)
dva=$(sed -Ene 's/^.+DVA\[0\]=<([^>]+)>.*/\1/p' <<< "$output")
log_note "first L1 block $init_data has a DVA of $dva"
output=$(zdb -ddddddbbbbbb $TESTPOOL/$TESTFS $obj 2> /dev/null \
    |grep -m 1 "L0 DVA" |head -n1)
blk_out0=${output##*>}
blk_out0=${blk_out0##+([[:space:]])}

output=$(zdb -ddddddbbbbbb $TESTPOOL/$TESTFS $obj 2> /dev/null \
    |grep -m 1 "1000  L0 DVA" |head -n1)
blk_out1=${output##*>}
blk_out1=${blk_out1##+([[:space:]])}

output=$(export ZDB_NO_ZLE=\"true\"; zdb -R $TESTPOOL $dva:bd\
    2> /dev/null)
output=${output##*>}
output=${output##+([[:space:]])}
if [ "$output" != "$blk_out0" ]; then
	log_fail "zdb -R :bd (block 0 display/decompress) failed"
fi

output=$(export ZDB_NO_ZLE=\"true\"; zdb -R $TESTPOOL $dva:db80\
    2> /dev/null)
output=${output##*>}
output=${output##+([[:space:]])}
if [ "$output" != "$blk_out1" ]; then
	log_fail "zdb -R :db80 (block 1 display/decompress) failed"
fi

output=$(export ZDB_NO_ZLE=\"true\"; zdb -R $TESTPOOL $dva:b80d\
    2> /dev/null)
output=${output##*>}
output=${output##+([[:space:]])}
if [ "$output" != "$blk_out1" ]; then
	log_fail "zdb -R :b80d (block 1 display/decompress) failed"
fi

vdev=$(echo "$dva" |awk '{split($0,array,":")} END{print array[1]}')
offset=$(echo "$dva" |awk '{split($0,array,":")} END{print array[2]}')
output=$(export ZDB_NO_ZLE=\"true\";\
    zdb -R $TESTPOOL $vdev:$offset:$l1_read_size:id 2> /dev/null)
block_cnt=$(echo "$output" | grep 'L0' | wc -l)
if [ $block_cnt -ne $write_count ]; then
	log_fail "zdb -R :id (indirect block display) failed"
fi

# read from specific half of mirror
vdev="$vdev.0"
log_note "Reading from DVA $vdev:$offset:$l1_read_size"
output=$(export ZDB_NO_ZLE=\"true\";\
    zdb -R $TESTPOOL $vdev:$offset:$l1_read_size:id 2> /dev/null)
block_cnt=$(echo "$output" | grep 'L0' | wc -l)
if [ $block_cnt -ne $write_count ]; then
        log_fail "zdb -R 0.0:offset:length:id (indirect block display) failed"
fi

log_pass "zdb -R :b flag (block display) works as expected"
