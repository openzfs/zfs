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
# ZFS should receive an unencrypted stream from an encrypted dataset
#
# STRATEGY:
# 1. Create an unencrypted dataset
# 2. Create an encrypted dataset
# 3. Create and checksum a file on the encrypted dataset
# 4. Snapshot the encrypted dataset
# 5. Attempt to receive the snapshot into an unencrypted child
# 6. Verify encryption is not enabled
# 7. Verify the checksum of the file is the same as the original
# 8. Attempt to receive the snapshot into an encrypted child
# 9. Verify the checksum of the file is the same as the original
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

log_assert "ZFS should receive an unencrypted stream from an encrypted dataset"

typeset passphrase="password"
typeset snap="$TESTPOOL/$TESTFS2@snap"

log_must zfs create $TESTPOOL/$TESTFS1
log_must eval "echo $passphrase | zfs create -o encryption=on" \
	"-o keyformat=passphrase $TESTPOOL/$TESTFS2"

log_must mkfile 1M /$TESTPOOL/$TESTFS2/$TESTFILE0
typeset checksum=$(md5digest /$TESTPOOL/$TESTFS2/$TESTFILE0)

log_must zfs snapshot $snap

log_note "Verify ZFS can receive into an unencrypted child"
log_must eval "zfs send $snap | zfs receive $TESTPOOL/$TESTFS1/c1"

crypt=$(get_prop encryption $TESTPOOL/$TESTFS1/c1)
[[ "$crypt" == "off" ]] || log_fail "Received unencrypted stream as encrypted"

typeset cksum1=$(md5digest /$TESTPOOL/$TESTFS1/c1/$TESTFILE0)
[[ "$cksum1" == "$checksum" ]] || \
	log_fail "Checksums differ ($cksum1 != $checksum)"

log_note "Verify ZFS can receive into an encrypted child"
log_must eval "zfs send $snap | zfs receive $TESTPOOL/$TESTFS2/c1"

typeset cksum2=$(md5digest /$TESTPOOL/$TESTFS2/c1/$TESTFILE0)
[[ "$cksum2" == "$checksum" ]] || \
	log_fail "Checksums differ ($cksum2 != $checksum)"

log_pass "ZFS can receive an unencrypted stream from an encrypted dataset"
