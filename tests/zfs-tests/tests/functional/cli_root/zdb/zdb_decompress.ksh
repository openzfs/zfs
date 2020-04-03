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
# zdb -R pool <DVA>:d will display the correct data and length
#
# Strategy:
# 1. Create a pool, set compression to lzjb
# 2. Write some identifiable data to a file
# 3. Run zdb -ddddddbbbbbb against the file
# 4. Record the DVA, lsize, and psize of L0 block 0
# 5. Run zdb -R with :d flag and match the data
# 6. Run zdb -R with :dr flags and match the lsize/psize
# 7. Run zdb -R with :dr flags and match the lsize
# 8. Run zdb -R with :dr flags and match the psize
#


function cleanup
{
	datasetexists $TESTPOOL && destroy_pool $TESTPOOL
}

log_assert "Verify zdb -R :d flag (decompress) works as expected"
log_onexit cleanup
init_data=$TESTDIR/file1
write_count=256
blksize=4096
pattern="_match__pattern_"
verify_runnable "global"
verify_disk_count "$DISKS" 2

default_mirror_setup_noexit $DISKS
log_must zfs set recordsize=$blksize $TESTPOOL/$TESTFS
log_must zfs set compression=lzjb $TESTPOOL/$TESTFS

# 16 chars 256 times = 4k = block size
typeset four_k=""
for i in {1..$write_count}
do
	four_k=$four_k$pattern
done

# write the 4k block 256 times
for i in {1..$write_count}
do
	echo $four_k >> $init_data
done

sync_pool $TESTPOOL true

# get object number of file
listing=$(ls -i $init_data)
set -A array $listing
obj=${array[0]}
log_note "file $init_data has object number $obj"

output=$(zdb -ddddddbbbbbb $TESTPOOL/$TESTFS $obj 2> /dev/null \
    |grep -m 1 "L0 DVA" |head -n1)
dva=$(sed -Ene 's/^.+DVA\[0\]=<([^>]+)>.*$/\1/p' <<< "$output")
log_note "block 0 of $init_data has a DVA of $dva"

# use the length reported by zdb -ddddddbbbbbb
size_str=$(sed -Ene 's/^.+ size=([^ ]+) .*$/\1/p' <<< "$output")
log_note "block size $size_str"

vdev=$(echo "$dva" |awk '{split($0,array,":")} END{print array[1]}')
offset=$(echo "$dva" |awk '{split($0,array,":")} END{print array[2]}')
output=$(zdb -R $TESTPOOL $vdev:$offset:$size_str:d 2> /dev/null)
echo $output |grep $pattern > /dev/null
(( $? != 0 )) && log_fail "zdb -R :d failed to decompress the data properly"

output=$(zdb -R $TESTPOOL $vdev:$offset:$size_str:dr 2> /dev/null)
echo $output |grep $four_k > /dev/null
(( $? != 0 )) && log_fail "zdb -R :dr failed to decompress the data properly"

output=$(zdb -R $TESTPOOL $vdev:$offset:$size_str:dr 2> /dev/null)
result=${#output}
(( $result != $blksize)) && log_fail \
"zdb -R failed to decompress the data to the length (${#output} != $size_str)"

# decompress using lsize
lsize=$(echo $size_str |awk '{split($0,array,"/")} END{print array[1]}')
psize=$(echo $size_str |awk '{split($0,array,"/")} END{print array[2]}')
output=$(zdb -R $TESTPOOL $vdev:$offset:$lsize:dr 2> /dev/null)
result=${#output}
(( $result != $blksize)) && log_fail \
"zdb -R failed to decompress the data (length ${#output} != $blksize)"

# Specifying psize will decompress successfully , but not always to full
# lsize since zdb has to guess lsize incrementally.
output=$(zdb -R $TESTPOOL $vdev:$offset:$psize:dr 2> /dev/null)
result=${#output}
# convert psize to decimal
psize_orig=$psize
psize=${psize%?}
psize=$((16#$psize))
(( $result < $psize)) && log_fail \
"zdb -R failed to decompress the data with psize $psize_orig\
 (length ${#output} < $psize)"

log_pass "zdb -R :d flag (decompress) works as expected"
