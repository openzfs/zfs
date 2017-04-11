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
# Verify datasets using mixed compression algorithms can be received.
#
# Strategy:
# 1. Write data with each of the available compression algorithms
# 2. Receive a full compressed send, and verify the data and compression ratios
#

verify_runnable "both"

log_assert "Verify datasets using mixed compression algorithms can be received."
log_onexit cleanup_pool $POOL2

send_ds=$POOL2/sendfs
recv_ds=$POOL2/recvfs

log_must zfs create $send_ds

for prop in "${compress_prop_vals[@]}"; do
	log_must zfs set compress=$prop $send_ds
	write_compressible $(get_prop mountpoint $send_ds) 16m
done

log_must zfs set compress=off $send_ds
log_must zfs snapshot $send_ds@full
log_must eval "zfs send -c $send_ds@full >$BACKDIR/full"
log_must eval "zfs recv $recv_ds <$BACKDIR/full"

verify_stream_size $BACKDIR/full $send_ds
verify_stream_size $BACKDIR/full $recv_ds
log_must cmp_ds_cont $send_ds $recv_ds

log_pass "Datasets using mixed compression algorithms can be received."
