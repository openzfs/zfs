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
# Copyright (c) 2015, Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/rsend/rsend.kshlib
. $STF_SUITE/include/properties.shlib

#
# Description:
# Verify a pool without the lz4 feature gracefully rejects a compressed stream
# because on any sending pool that supports it, metadata will be compressed
# with lz4 even if user data is not compressed.
#
# Strategy:
# 1. For each of an uncompressed, gzip and lz4 dataset, do the following
#    receives into a pool without the lz4 feature:
# 2. Attempt to receive the compressed stream (should fail)
# 3. Attempt to receive the uncompressed stream (should succeed)
#

verify_runnable "both"

log_assert "Verify compressed streams are rejected if incompatible."

typeset send_ds=$POOL2/testds
typeset recv_ds=$POOL3/testds

function cleanup
{
	poolexists $POOL2 && destroy_pool $POOL2
	poolexists $POOL3 && destroy_pool $POOL3
	log_must zpool create $POOL2 $DISK2
}
log_onexit cleanup

datasetexists $POOL3 && log_must zpool destroy $POOL3
log_must zpool create -d $POOL3 $DISK3

for compress in "${compress_prop_vals[@]}"; do
	datasetexists $send_ds && destroy_dataset $send_ds -r
	datasetexists $recv_ds && destroy_dataset $recv_ds -r

	log_must zfs create -o compress=$compress $send_ds
	typeset dir=$(get_prop mountpoint $send_ds)
	write_compressible $dir 16m
	log_must zfs snapshot $send_ds@full

	log_must eval "zfs send -c $send_ds@full >$BACKDIR/full-c"
	log_mustnot eval "zfs recv $recv_ds <$BACKDIR/full-c"

	log_must eval "zfs send $send_ds@full >$BACKDIR/full"
	log_must eval "zfs recv $recv_ds <$BACKDIR/full"
done

log_pass "Compressed streams are rejected if incompatible."
