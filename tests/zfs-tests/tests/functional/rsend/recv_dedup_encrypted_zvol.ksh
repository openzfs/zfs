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
# Copyright (c) 2020 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/rsend/rsend.kshlib

#
# DESCRIPTION:
# Verifies that we can receive a dedup send stream of a zvol by processing it
# with "zstream redup".
#

verify_runnable "both"

function cleanup
{
	destroy_dataset $TESTPOOL/recv "-r"
	rm $sendfile
	rm $volfile
	rm $keyfile
}
log_onexit cleanup

log_assert "Verify zfs can receive raw, recursive, and deduplicated send streams"

typeset keyfile=/$TESTPOOL/pkey
typeset recvdev=$ZVOL_DEVDIR/$TESTPOOL/recv
typeset sendfile_compressed=$STF_SUITE/tests/functional/rsend/dedup_encrypted_zvol.zsend.bz2
typeset sendfile=/$TESTPOOL/dedup_encrypted_zvol.zsend
typeset volfile_compressed=$STF_SUITE/tests/functional/rsend/dedup_encrypted_zvol.bz2
typeset volfile=/$TESTPOOL/dedup_encrypted_zvol

log_must eval "echo 'password' > $keyfile"

log_must eval "bzcat <$sendfile_compressed >$sendfile"
log_must eval "zstream redup $sendfile | zfs recv $TESTPOOL/recv"

log_must zfs load-key $TESTPOOL/recv
block_device_wait $volfile

log_must eval "bzcat <$volfile_compressed >$volfile"
log_must diff $volfile $recvdev

log_pass "zfs can receive raw, recursive, and deduplicated send streams"
