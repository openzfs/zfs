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
. $STF_SUITE/tests/functional/direct/dio.cfg
. $STF_SUITE/tests/functional/direct/dio.kshlib

#
# DESCRIPTION:
# A Direct I/O read of a block that fails to decompress returns EIO and
# leaves none of the block's data in the caller's buffer.
#
# STRATEGY:
# 1. Create a pool with an lz4 compressed dataset.
# 2. Write a file of compressible text with Direct I/O, so that none of
#    its blocks is cached, and check that its first block is compressed.
# 3. Inject a decompression error on every read of the file.
# 4. Read the first block with Direct I/O into a buffer filled with FILL.
# 5. Verify that the read failed with EIO, was a Direct I/O read, and
#    left only FILL or zeros in the buffer.
# 6. Clear the injection and verify the file.
#

verify_runnable "global"

BS=$((128 * 1024))
NBLOCKS=4
# Neither zero nor a byte of the text written (0xa5)
FILL=165

function cleanup
{
	zinject -c all
	dio_cleanup
}

#
# Write size bytes of numbered 32-byte lines of text to file.
#
function write_text # file size
{
	python3 -c '
import sys
with open(sys.argv[1], "wb") as f:
    for i in range(int(sys.argv[2]) // 32):
        f.write(b"dio_decompress_fault %010d\n" % i)
' "$1" "$2"
}

#
# Read the first BS bytes of file with O_DIRECT into a page-aligned buffer
# filled with FILL, then print the read's errno, the number of bytes of the
# buffer that are neither FILL nor zero, the offset of the first of them,
# and the 64 bytes from it in hex.
#
function read_into_filled_buffer # file
{
	python3 -c '
import mmap, os, sys
bs, fill = int(sys.argv[2]), int(sys.argv[3])
buf = mmap.mmap(-1, bs)
buf.write(bytes([fill]) * bs)
fd = os.open(sys.argv[1], os.O_RDONLY | os.O_DIRECT)
err = 0
try:
    os.preadv(fd, [buf], 0)
except OSError as e:
    err = e.errno
os.close(fd)
data = bytes(buf)
other = [i for i, b in enumerate(data) if b != 0 and b != fill]
first = other[0] if other else -1
dump = data[first:first + 64].hex() if other else "-"
print("%d %d %d %s" % (err, len(other), first, dump))
' "$1" $BS $FILL
}

log_assert "A Direct I/O read that fails to decompress leaves no data behind"

log_onexit cleanup

log_must truncate -s $MINVDEVSIZE $DIO_VDEVS
create_pool $TESTPOOL1 $DIO_VDEVS
log_must zfs create -o compression=lz4 -o recordsize=128k \
    -o direct=standard $TESTPOOL1/$TESTFS1
mntpnt=$(get_prop mountpoint $TESTPOOL1/$TESTFS1)
text=$mntpnt/text
file=$mntpnt/file

log_must write_text $text $((BS * NBLOCKS))
log_must stride_dd -i $text -o $file -b $BS -c $NBLOCKS -D
sync_pool $TESTPOOL1

obj=$(get_objnum $file)
sizes=$(zdb -ddddd $TESTPOOL1/$TESTFS1 $obj | \
    awk '$1 == "0" && $2 == "L0" { print $4 }')
log_note "First block of $file: $sizes"
lsize=${sizes%%L/*}
psize=${sizes#*L/}
psize=${psize%P}
if [[ -z $lsize || -z $psize || $sizes != *L/*P ]] ||
    ! (( 16#$psize < 16#$lsize )); then
	log_fail "The first block of $file is not compressed: $sizes"
fi

log_must zinject -t data -e decompress -f 100 $file

prev_dio_rd=$(kstat_pool $TESTPOOL1 iostats.direct_read_count)
result=$(read_into_filled_buffer $file)
curr_dio_rd=$(kstat_pool $TESTPOOL1 iostats.direct_read_count)
log_must zinject -c all

log_note "errno, other bytes, first offset, 64 bytes from it: $result"
set -A fields $result
if [[ ${fields[0]} != 5 ]]; then
	log_fail "The read returned errno ${fields[0]}, not EIO (5)"
fi
if (( curr_dio_rd - prev_dio_rd < 1 )); then
	kstat_pool -g $TESTPOOL1 iostats
	log_fail "The read was not a Direct I/O read"
fi
if [[ ${fields[1]} != 0 ]]; then
	log_fail "The failed read left ${fields[1]} bytes other than $FILL" \
	    "or zero in the buffer, the first at offset ${fields[2]}"
fi

log_must cmp $text $file

log_pass "A Direct I/O read that fails to decompress leaves no data behind"
