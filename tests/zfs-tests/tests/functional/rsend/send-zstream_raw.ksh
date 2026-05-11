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
# http://www.illumos.org/license/CDDL.
#

#
# Copyright (c) 2026 Klara, Inc.
#

# TODO: audit these libs
. $STF_SUITE/tests/functional/rsend/rsend.kshlib
. $STF_SUITE/include/math.shlib

#
# Description:
# Verify that "zstream raw" can recreate a zvol from a stream
#
# Strategy:
# 1. Create a zvol and initialize it with some data
# 2. Send the zvol and transform it into an image file with zstream raw
# 3. Verify the contents of the image file match the zvol
# 4. Add changes to the zvol and verify an incremental stream
# 5. Repeat for a stream package with multiple intermediary snapshots

verify_runnable "both"

log_assert "Verify zstream raw recreates a zvol from a stream"

typeset volume=$TESTPOOL/zvol
typeset volsize=256m
typeset image=$TEST_BASE_DIR/zvol.img
typeset image1=$TEST_BASE_DIR/zvol1.img

function cleanup
{
	datasetexists $volume && destroy_dataset $volume -Rr
	rm -f $image $image1
}

log_onexit cleanup

function randwrite
{
	dd \
	    if=/dev/urandom \
	    of=$ZVOL_DEVDIR/$volume \
	    oflag=sync \
	    conv=notrunc \
	    bs=128k \
	    $@
}

# 1. Create a zvol and initialize it with some data
log_must zfs create -V $volsize -o volmode=dev $volume
block_device_wait $ZVOL_DEVDIR/$volume
log_must randwrite count=2

# 2. Send the zvol and transform it into an image file with zstream raw
log_must zfs snapshot $volume@snapshot
log_must eval "guid=\$(zfs send $volume@snapshot | zstream raw $image)"

# 3. Verify the contents of the image file match the zvol
log_must cmp_xxh128 $ZVOL_DEVDIR/$volume $image

# 4. Add changes to the zvol and verify an incremental stream
log_must randwrite seek=64 count=24
log_must zfs snapshot $volume@snapshot1
log_must eval "guid=\$(zfs send -i @snapshot $volume@snapshot1 |
    zstream raw -g $guid $image)"
log_must cmp_xxh128 $ZVOL_DEVDIR/$volume $image

# 5. Repeat for a stream package with multiple intermediary snapshots
log_must cp $image $image1
typeset -i nsnaps=5
for i in $(seq 2 $nsnaps); do
	# TODO: replace randwrite with a mixed workload
	log_must randwrite seek=$((128 * i)) count=$((16 * i))
	log_must zfs snapshot $volume@snapshot$i
	# Also verify consecutively applied individual incremental streams.
	log_must eval "zfs send -i @snapshot$((i - 1)) $volume@snapshot$i |
	    zstream raw $image1"
	log_must cmp_xxh128 $ZVOL_DEVDIR/$volume $image1
done
log_must eval "zfs send -I @snapshot1 $volume@snapshot$nsnaps |
    zstream raw -vg $guid $image"
log_must cmp_xxh128 $ZVOL_DEVDIR/$volume $image

log_pass "zstream raw recreates a zvol from a stream."
