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
# 'zfs clone' should create encrypted clones of encrypted datasets
#
# STRATEGY:
# 1. Create an encrypted dataset
# 2. Create a snapshot of the dataset
# 3. Attempt to clone the snapshot as an unencrypted dataset
# 4. Attempt to clone the snapshot with a new key
# 5. Attempt to clone the snapshot as a child of an unencrypted dataset
# 6. Attempt to clone the snapshot as a child of an encrypted dataset
# 7. Verify the encryption root of the datasets
# 8. Unmount all datasets and unload their keys
# 9. Attempt to load the encryption root's key
# 10. Verify each dataset's key is loaded
# 11. Attempt to mount each dataset
#

verify_runnable "both"

function cleanup
{
	datasetexists $TESTPOOL/$TESTFS2 && \
		log_must zfs destroy -f $TESTPOOL/$TESTFS2
	datasetexists $TESTPOOL/$TESTFS1 && \
		log_must zfs destroy -r $TESTPOOL/$TESTFS1
}
log_onexit cleanup

log_assert "'zfs clone' should create encrypted clones of encrypted datasets"

log_must eval "echo $PASSPHRASE | zfs create -o encryption=on" \
	"-o keyformat=passphrase -o keylocation=prompt $TESTPOOL/$TESTFS1"
log_must zfs snapshot $TESTPOOL/$TESTFS1@now

log_mustnot zfs clone -o encryption=off $TESTPOOL/$TESTFS1@now \
	$TESTPOOL/$TESTFS2
log_mustnot eval "echo $PASSPHRASE1 | zfs clone -o keyformat=passphrase" \
	"$TESTPOOL/$TESTFS1@now $TESTPOOL/$TESTFS2"
log_must zfs clone $TESTPOOL/$TESTFS1@now $TESTPOOL/$TESTFS2
log_must zfs clone $TESTPOOL/$TESTFS1@now $TESTPOOL/$TESTFS1/child

log_must verify_encryption_root $TESTPOOL/$TESTFS2 $TESTPOOL/$TESTFS1
log_must verify_encryption_root $TESTPOOL/$TESTFS1/child $TESTPOOL/$TESTFS1

log_must zfs unmount $TESTPOOL/$TESTFS1
log_must zfs unmount $TESTPOOL/$TESTFS2
log_must zfs unload-key -a

log_must eval "echo $PASSPHRASE | zfs load-key $TESTPOOL/$TESTFS1"

log_must key_available $TESTPOOL/$TESTFS1
log_must key_available $TESTPOOL/$TESTFS1/child
log_must key_available $TESTPOOL/$TESTFS2

log_must zfs mount $TESTPOOL/$TESTFS1
log_must zfs mount $TESTPOOL/$TESTFS1/child
log_must zfs mount $TESTPOOL/$TESTFS2

log_pass "'zfs clone' creates encrypted clones of encrypted datasets"
