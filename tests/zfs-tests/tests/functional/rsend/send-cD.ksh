#!/bin/ksh -p

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
# Copyright (c) 2015, 2018 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/rsend/rsend.kshlib

#
# Description:
# Verify that the -c and -D flags do not interfere with each other.
#
# Strategy:
# 1. Write unique data to a filesystem and create a compressed, deduplicated
#    full stream.
# 2. Verify that the stream and send dataset show the same size
# 3. Make several copies of the original data, and create both full and
#    incremental compressed, deduplicated send streams
# 4. Verify the full stream is no bigger than the stream from step 1
# 5. Verify the streams can be received correctly.
#

verify_runnable "both"

log_assert "Verify that the -c and -D flags do not interfere with each other"
log_onexit cleanup_pool $POOL2

typeset sendfs=$POOL2/sendfs
typeset recvfs=$POOL2/recvfs
typeset stream0=$BACKDIR/stream.0
typeset stream1=$BACKDIR/stream.1
typeset inc=$BACKDIR/stream.inc

log_must zfs create -o compress=lz4 $sendfs
log_must zfs create -o compress=lz4 $recvfs
typeset dir=$(get_prop mountpoint $sendfs)
# Don't use write_compressible: we want compressible but undeduplicable data.
log_must eval "dd if=/dev/urandom bs=1024k count=4 | base64 >$dir/file"
log_must zfs snapshot $sendfs@snap0
log_must eval "zfs send -D -c $sendfs@snap0 >$stream0"

# The stream size should match at this point because the data is all unique
verify_stream_size $stream0 $sendfs

for i in {0..3}; do
	log_must cp $dir/file $dir/file.$i
done
log_must zfs snapshot $sendfs@snap1

# The stream sizes should match, since the second stream contains no new blocks
log_must eval "zfs send -D -c $sendfs@snap1 >$stream1"
typeset size0=$(stat -c %s $stream0)
typeset size1=$(stat -c %s $stream1)
within_percent $size0 $size1 90 || log_fail "$size0 and $size1"

# Finally, make sure the receive works correctly.
log_must eval "zfs send -D -c -i snap0 $sendfs@snap1 >$inc"
log_must eval "zfs recv -d $recvfs <$stream0"
log_must eval "zfs recv -d $recvfs <$inc"
cmp_ds_cont $sendfs $recvfs

# The size of the incremental should be the same as the initial send.
typeset size2=$(stat -c %s $inc)
within_percent $size0 $size2 90 || log_fail "$size0 and $size1"

log_pass "The -c and -D flags do not interfere with each other"
