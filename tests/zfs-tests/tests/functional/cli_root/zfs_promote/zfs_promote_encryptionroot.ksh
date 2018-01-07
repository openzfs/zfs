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
. $STF_SUITE/tests/functional/cli_root/zfs_load-key/zfs_load-key_common.kshlib

#
# DESCRIPTION:
# ZFS must promote clones of an encryption root.
#
# STRATEGY:
# 1. Create an encrypted dataset
# 2. Clone the encryption root
# 3. Clone the clone
# 4. Verify the encryption root of all three datasets is the origin
# 5. Promote the clone of the clone
# 6. Verify the encryption root of all three datasets is still the origin
# 7. Promote the clone of the original encryption root
# 8. Verify the encryption root of all three datasets is the promoted dataset
#

verify_runnable "both"

function cleanup
{
	datasetexists $TESTPOOL/$TESTFS1 && \
		log_must zfs destroy -Rf $TESTPOOL/$TESTFS1
	datasetexists $TESTPOOL/clone1 && \
		log_must zfs destroy -Rf $TESTPOOL/clone1
	datasetexists $TESTPOOL/clone2 && \
		log_must zfs destroy -Rf $TESTPOOL/clone2
}
log_onexit cleanup

log_assert "ZFS must promote clones of an encryption root"

passphrase="password"
snaproot="$TESTPOOL/$TESTFS1@snap1"
snapclone="$TESTPOOL/clone1@snap2"

log_must eval "echo $passphrase | zfs create -o encryption=on" \
	"-o keyformat=passphrase $TESTPOOL/$TESTFS1"

log_must zfs snap $snaproot
log_must zfs clone $snaproot $TESTPOOL/clone1
log_must zfs snap $snapclone
log_must zfs clone $snapclone $TESTPOOL/clone2

log_must verify_encryption_root $TESTPOOL/$TESTFS1 $TESTPOOL/$TESTFS1
log_must verify_encryption_root $TESTPOOL/clone1 $TESTPOOL/$TESTFS1
log_must verify_encryption_root $TESTPOOL/clone2 $TESTPOOL/$TESTFS1

log_must zfs promote $TESTPOOL/clone2
log_must verify_encryption_root $TESTPOOL/$TESTFS1 $TESTPOOL/$TESTFS1
log_must verify_encryption_root $TESTPOOL/clone1 $TESTPOOL/$TESTFS1
log_must verify_encryption_root $TESTPOOL/clone2 $TESTPOOL/$TESTFS1

log_must zfs promote $TESTPOOL/clone2
log_must verify_encryption_root $TESTPOOL/$TESTFS1 $TESTPOOL/clone2
log_must verify_encryption_root $TESTPOOL/clone1 $TESTPOOL/clone2
log_must verify_encryption_root $TESTPOOL/clone2 $TESTPOOL/clone2

log_pass "ZFS promotes clones of an encryption root"
