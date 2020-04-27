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
# Copyright (c) 2015 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/rsend/rsend.kshlib
. $STF_SUITE/include/properties.shlib

#
# Description:
# Verify that compressed streams can contain embedded blocks.
#
# Strategy:
# 1. Create a filesystem with compressible data and embedded blocks.
# 2. Verify the created streams can be received correctly.
# 3. Verify the presence / absence of embedded blocks in the compressed stream,
#    as well as the receiving file system.
#

verify_runnable "both"

log_assert "Verify that compressed streams can contain embedded blocks."
log_onexit cleanup_pool $POOL2

typeset objs obj recsize
typeset sendfs=$POOL2/sendfs
typeset recvfs=$POOL2/recvfs
typeset stream=$BACKDIR/stream
typeset dump=$BACKDIR/dump
typeset recvfs2=$POOL2/recvfs2
typeset stream2=$BACKDIR/stream2
typeset dump2=$BACKDIR/dump2
log_must zfs create -o compress=lz4 $sendfs
log_must zfs create -o compress=lz4 $recvfs
log_must zfs create -o compress=lz4 $recvfs2
typeset dir=$(get_prop mountpoint $sendfs)

# Populate the send dataset with compressible data and embedded block files.
write_compressible $dir 16m
for recsize in "${recsize_prop_vals[@]}"; do
	# For lz4, this method works for blocks up to 16k, but not larger
	[[ $recsize -eq $((32 * 1024)) ]] && break

	if is_linux || is_freebsd; then
		log_must truncate -s $recsize $dir/$recsize
		log_must dd if=/dev/urandom of=$dir/$recsize \
		    seek=$((recsize - 8)) bs=1 count=8 conv=notrunc
	else
		log_must mkholes -h 0:$((recsize - 8)) -d $((recsize - 8)):8 \
		    $dir/$recsize
	fi
done

# Generate the streams and zstreamdump output.
log_must zfs snapshot $sendfs@now
log_must eval "zfs send -c $sendfs@now >$stream"
log_must eval "zstreamdump -v <$stream >$dump"
log_must eval "zfs recv -d $recvfs <$stream"
cmp_ds_cont $sendfs $recvfs
verify_stream_size $stream $sendfs
log_mustnot stream_has_features $stream embed_data

log_must eval "zfs send -c -e $sendfs@now >$stream2"
log_must eval "zstreamdump -v <$stream2 >$dump2"
log_must eval "zfs recv -d $recvfs2 <$stream2"
cmp_ds_cont $sendfs $recvfs2
verify_stream_size $stream2 $sendfs
log_must stream_has_features $stream2 embed_data

# Verify embedded blocks are present only when expected.
for recsize in "${recsize_prop_vals[@]}"; do
	[[ $recsize -eq $((32 * 1024)) ]] && break

	typeset send_obj=$(get_objnum $(get_prop mountpoint $sendfs)/$recsize)
	typeset recv_obj=$(get_objnum \
	    $(get_prop mountpoint $recvfs/sendfs)/$recsize)
	typeset recv2_obj=$(get_objnum \
	    $(get_prop mountpoint $recvfs2/sendfs)/$recsize)

	log_must eval "zdb -ddddd $sendfs $send_obj >$BACKDIR/sendfs.zdb"
	log_must eval "zdb -ddddd $recvfs/sendfs $recv_obj >$BACKDIR/recvfs.zdb"
	log_must eval "zdb -ddddd $recvfs2/sendfs $recv2_obj >$BACKDIR/recvfs2.zdb"

	grep -q "EMBEDDED" $BACKDIR/sendfs.zdb || \
	    log_fail "Obj $send_obj not embedded in $sendfs"
	grep -q "EMBEDDED" $BACKDIR/recvfs.zdb || \
	    log_fail "Obj $recv_obj not embedded in $recvfs"
	grep -q "EMBEDDED" $BACKDIR/recvfs2.zdb || \
	    log_fail "Obj $recv2_obj not embedded in $recvfs2"

	grep -q "WRITE_EMBEDDED object = $send_obj offset = 0" $dump && \
	    log_fail "Obj $obj embedded in zstreamdump output"
	grep -q "WRITE_EMBEDDED object = $send_obj offset = 0" $dump2 || \
	    log_fail "Obj $obj not embedded in zstreamdump output"
done

log_pass "Compressed streams can contain embedded blocks."
