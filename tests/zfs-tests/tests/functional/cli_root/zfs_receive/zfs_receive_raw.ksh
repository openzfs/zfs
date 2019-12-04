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
# ZFS should receive streams from raw sends.
#
# STRATEGY:
# 1. Create an encrypted dataset
# 2. Create a file and get its checksum
# 3. Snapshot the dataset
# 4. Attempt to receive a raw send stream as a child of an unencrypted dataset
# 5. Verify the key is unavailable
# 6. Attempt to load the key and mount the dataset
# 7. Verify the checksum of the file is the same as the original
# 8. Attempt to receive a raw send stream as a child of an encrypted dataset
# 9. Verify the key is unavailable
# 10. Attempt to load the key and mount the dataset
# 11. Verify the checksum of the file is the same as the original
# 12. Verify 'zfs receive -n' works with the raw stream
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

log_assert "ZFS should receive streams from raw sends"

typeset passphrase="password"
typeset snap="$TESTPOOL/$TESTFS1@snap"

log_must eval "echo $passphrase | zfs create -o encryption=on" \
	"-o keyformat=passphrase $TESTPOOL/$TESTFS1"

log_must mkfile 1M /$TESTPOOL/$TESTFS1/$TESTFILE0
typeset checksum=$(md5digest /$TESTPOOL/$TESTFS1/$TESTFILE0)

log_must zfs snapshot $snap

log_note "Verify ZFS can receive a raw send stream from an encrypted dataset"
log_must eval "zfs send -w $snap | zfs receive $TESTPOOL/$TESTFS2"

keystatus=$(get_prop keystatus $TESTPOOL/$TESTFS2)
[[ "$keystatus" == "unavailable" ]] || \
	log_fail "Expected keystatus unavailable, got $keystatus"

log_must eval "echo $passphrase | zfs mount -l $TESTPOOL/$TESTFS2"

typeset cksum1=$(md5digest /$TESTPOOL/$TESTFS2/$TESTFILE0)
[[ "$cksum1" == "$checksum" ]] || \
	log_fail "Checksums differ ($cksum1 != $checksum)"

log_must eval "zfs send -w $snap | zfs receive $TESTPOOL/$TESTFS1/c1"

keystatus=$(get_prop keystatus $TESTPOOL/$TESTFS1/c1)
[[ "$keystatus" == "unavailable" ]] || \
	log_fail "Expected keystatus unavailable, got $keystatus"

log_must eval "echo $passphrase | zfs mount -l $TESTPOOL/$TESTFS1/c1"
typeset cksum2=$(md5digest /$TESTPOOL/$TESTFS1/c1/$TESTFILE0)
[[ "$cksum2" == "$checksum" ]] || \
	log_fail "Checksums differ ($cksum2 != $checksum)"

log_must eval "zfs send -w $snap | zfs receive -n $TESTPOOL/$TESTFS3"

log_pass "ZFS can receive streams from raw sends"
