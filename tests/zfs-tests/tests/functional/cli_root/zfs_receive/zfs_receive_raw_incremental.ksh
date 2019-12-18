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
# 6. Change the passphrase required to unlock the original filesystem
# 7. Attempt and intentionally fail to receive the second snapshot
# 8. Verify that the required passphrase hasn't changed on the receive side
# 9. Attempt a real raw incremental send stream of the second snapshot
# 10. Attempt load the key and mount the dataset
# 11. Verify the checksum of the file is the same as the original
#

verify_runnable "both"

function cleanup
{
	datasetexists $TESTPOOL/$TESTFS1 && \
		log_must zfs destroy -r $TESTPOOL/$TESTFS1

	datasetexists $TESTPOOL/$TESTFS2 && \
		log_must zfs destroy -r $TESTPOOL/$TESTFS2

	[[ -f $ibackup ]] && log_must rm -f $ibackup
	[[ -f $ibackup_trunc ]] && log_must rm -f $ibackup_trunc
}

log_onexit cleanup

log_assert "ZFS should receive streams from raw incremental sends"

typeset ibackup="$TEST_BASE_DIR/ibackup.$$"
typeset ibackup_trunc="$TEST_BASE_DIR/ibackup_trunc.$$"
typeset passphrase="password"
typeset passphrase2="password2"
typeset snap1="$TESTPOOL/$TESTFS1@snap1"
typeset snap2="$TESTPOOL/$TESTFS1@snap2"

log_must eval "echo $passphrase | zfs create -o encryption=on" \
	"-o keyformat=passphrase $TESTPOOL/$TESTFS1"

log_must zfs snapshot $snap1

log_must mkfile 1M /$TESTPOOL/$TESTFS1/$TESTFILE0
typeset checksum=$(md5digest /$TESTPOOL/$TESTFS1/$TESTFILE0)

log_must zfs snapshot $snap2

log_must eval "zfs send -w $snap1 | zfs receive $TESTPOOL/$TESTFS2"
log_must eval "echo $passphrase2 | zfs change-key $TESTPOOL/$TESTFS1"
log_must eval "zfs send -w -i $snap1 $snap2 > $ibackup"

typeset trunc_size=$(stat_size $ibackup)
trunc_size=$(expr $trunc_size - 64)
log_must cp $ibackup $ibackup_trunc
log_must truncate -s $trunc_size $ibackup_trunc
log_mustnot eval "zfs receive $TESTPOOL/$TESTFS2 < $ibackup_trunc"
log_mustnot eval "echo $passphrase2 | zfs load-key $TESTPOOL/$TESTFS2"
log_must eval "echo $passphrase | zfs load-key $TESTPOOL/$TESTFS2"
log_must zfs unload-key $TESTPOOL/$TESTFS2

log_must eval "zfs receive $TESTPOOL/$TESTFS2 < $ibackup"
log_must eval "echo $passphrase2 | zfs mount -l $TESTPOOL/$TESTFS2"

typeset cksum1=$(md5digest /$TESTPOOL/$TESTFS2/$TESTFILE0)
[[ "$cksum1" == "$checksum" ]] || \
	log_fail "Checksums differ ($cksum1 != $checksum)"

log_pass "ZFS can receive streams from raw incremental sends"
