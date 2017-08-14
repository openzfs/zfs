#!/bin/ksh -p
#
# CDDL HEADER START
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
# CDDL HEADER END
#

#
# Copyright (c) 2017 Datto, Inc. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# ZFS should receive streams from raw incremental sends.
#
# STRATEGY:
# 1. Create an encrypted dataset
# 2. Snapshot the dataset
# 3. Create a file and get its checksum
# 4. Snapshot the dataset
# 5. Attempt to receive a raw send stream of the first snapshot
# 6. Attempt to receive a raw incremental send stream of the second snapshot
# 7. Attempt load the key and mount the dataset
# 8. Verify the cheksum of the file is the same as the original
#

verify_runnable "both"

function cleanup
{
	datasetexists $TESTPOOL/$TESTFS1 && \
		log_must zfs destroy -r $TESTPOOL/$TESTFS1

	datasetexists $TESTPOOL/$TESTFS2 && \
		log_must zfs destroy -r $TESTPOOL/$TESTFS2
}

log_onexit cleanup

log_assert "ZFS should receive streams from raw incremental sends"

typeset passphrase="password"
typeset snap1="$TESTPOOL/$TESTFS1@snap1"
typeset snap2="$TESTPOOL/$TESTFS1@snap2"

log_must eval "echo $passphrase | zfs create -o encryption=on" \
	"-o keyformat=passphrase $TESTPOOL/$TESTFS1"

log_must zfs snapshot $snap1

log_must mkfile 1M /$TESTPOOL/$TESTFS1/$TESTFILE0
typeset checksum=$(md5sum /$TESTPOOL/$TESTFS1/$TESTFILE0 | awk '{ print $1 }')

log_must zfs snapshot $snap2

log_must eval "zfs send -w $snap1 | zfs receive $TESTPOOL/$TESTFS2"
log_must eval "zfs send -w -i $snap1 $snap2 | zfs receive $TESTPOOL/$TESTFS2"
log_must eval "echo $passphrase | zfs mount -l $TESTPOOL/$TESTFS2"

typeset cksum1=$(md5sum /$TESTPOOL/$TESTFS2/$TESTFILE0 | awk '{ print $1 }')
[[ "$cksum1" == "$checksum" ]] || \
	log_fail "Checksums differ ($cksum1 != $checksum)"

log_pass "ZFS can receive streams from raw incremental sends"
