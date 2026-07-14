#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
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
# Copyright (c) 2026 by Andrew Mochalskyi. All rights reserved.
#

. $STF_SUITE/tests/functional/rsend/rsend.kshlib

#
# DESCRIPTION:
#	A raw send of an encrypted dataset that carries snapshot holds but no
#	properties (zfs send -w -h, without -p/-R) must be received without
#	aborting.  Such a stream has no "props" nvlist, and the receive-side
#	encryption-root fixup used to dereference it unconditionally and abort
#	(https://github.com/openzfs/zfs/issues/10787).
#
# STRATEGY:
#	1. Create an encrypted dataset with some data and a held snapshot.
#	2. Raw send it with holds, receive it, and confirm the receive
#	   succeeds and the hold is recreated.
#	3. Repeat for an incremental raw send with holds.
#

verify_runnable "both"

function cleanup
{
	eval "zfs holds -H $snap 2>/dev/null | grep -q hold1" &&
	    log_must zfs release hold1 $snap
	eval "zfs holds -H $snap2 2>/dev/null | grep -q hold2" &&
	    log_must zfs release hold2 $snap2
	eval "zfs holds -H $recvsnap 2>/dev/null | grep -q hold1" &&
	    log_must zfs release hold1 $recvsnap
	eval "zfs holds -H $recvsnap2 2>/dev/null | grep -q hold2" &&
	    log_must zfs release hold2 $recvsnap2
	datasetexists $TESTPOOL/recv && destroy_dataset $TESTPOOL/recv "-r"
	datasetexists $TESTPOOL/crypt && destroy_dataset $TESTPOOL/crypt "-r"
	[[ -f $keyfile ]] && log_must rm -f $keyfile
	[[ -f $sendfile ]] && log_must rm -f $sendfile
}
log_onexit cleanup

log_assert "Raw send of an encrypted dataset with holds is received cleanly"

typeset keyfile=/$TESTPOOL/pkey.holds
typeset sendfile=$TEST_BASE_DIR/send_encrypted_holds.$$
typeset snap=$TESTPOOL/crypt@snap1
typeset snap2=$TESTPOOL/crypt@snap2
typeset recvsnap=$TESTPOOL/recv@snap1
typeset recvsnap2=$TESTPOOL/recv@snap2

log_must eval "echo 'password' > $keyfile"

log_must zfs create -o encryption=on -o keyformat=passphrase \
    -o keylocation=file://$keyfile $TESTPOOL/crypt
log_must mkfile 1m /$TESTPOOL/crypt/file1
log_must zfs snapshot $snap
log_must zfs hold hold1 $snap

log_note "Full raw send with holds is received without aborting"
log_must eval "zfs send -w -h $snap > $sendfile"
log_must eval "zfs receive $TESTPOOL/recv < $sendfile"
log_must datasetexists $recvsnap
log_must eval "zfs holds -H $recvsnap | grep -q hold1"

log_note "Incremental raw send with holds is received without aborting"
log_must mkfile 1m /$TESTPOOL/crypt/file2
log_must zfs snapshot $snap2
log_must zfs hold hold2 $snap2
log_must eval "zfs send -w -h -i $snap $snap2 > $sendfile"
log_must eval "zfs receive $TESTPOOL/recv < $sendfile"
log_must datasetexists $recvsnap2
log_must eval "zfs holds -H $recvsnap2 | grep -q hold2"

log_pass "Raw send of an encrypted dataset with holds is received cleanly"
