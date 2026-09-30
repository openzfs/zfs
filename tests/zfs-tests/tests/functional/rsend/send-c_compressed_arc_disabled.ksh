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

. $STF_SUITE/tests/functional/rsend/rsend.kshlib

#
# Description:
# Verify compressed and raw send streams contain valid data when the
# blocks are cached uncompressed in the ARC (zfs_compressed_arc_enabled=0).
#
# Strategy:
# 1. Disable the compressed ARC.
# 2. Write compressible data to an lz4 dataset and keep it cached.
# 3. Send it with -c and -w and receive both streams.
# 4. Reimport the pool so the data is read back from disk.
# 5. Verify the received files match the source.
#

verify_runnable "both"

log_assert "Compressed sends are valid with the compressed ARC disabled."

typeset send_ds=$POOL2/testds
typeset orig_carc=$(get_tunable COMPRESSED_ARC_ENABLED)

function cleanup
{
	log_must set_tunable64 COMPRESSED_ARC_ENABLED $orig_carc
	datasetexists $send_ds && destroy_dataset $send_ds -r
	for flag in c w; do
		datasetexists $POOL2/recv-$flag && \
		    destroy_dataset $POOL2/recv-$flag -r
	done
}
log_onexit cleanup

log_must set_tunable64 COMPRESSED_ARC_ENABLED 0

log_must zfs create -o compress=lz4 $send_ds
typeset src=$(get_prop mountpoint $send_ds)
write_compressible $src 8m
log_must eval "cat $src/file.0 >/dev/null"
log_must zfs snapshot $send_ds@snap

for flag in c w; do
	log_must eval "zfs send -$flag $send_ds@snap >$BACKDIR/stream-$flag"
	log_must eval "zfs recv $POOL2/recv-$flag <$BACKDIR/stream-$flag"
done

log_must zpool export $POOL2
log_must zpool import $POOL2

for flag in c w; do
	log_must cmp $src/file.0 $(get_prop mountpoint $POOL2/recv-$flag)/file.0
done

log_pass "Compressed sends are valid with the compressed ARC disabled."
