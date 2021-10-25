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
# 'zfs change-key' should promote an encrypted child to an encryption root.
#
# STRATEGY:
# 1. Create an encrypted dataset
# 2. Create an encrypted child dataset
# 3. Create an unencrypted child dataset
# 4. Attempt to change the key without any flags
# 5. Attempt to change the key specifying keylocation
# 6. Attempt to change the key specifying keyformat
# 7. Verify the new encryption root can unload and load its key
# 8. Recreate the child dataset
# 9. Attempt to change the key specifying both the keylocation and keyformat
# 10. Verify the new encryption root can unload and load its key
# 11. Verify the unencrytped child is still accessible normally
#

verify_runnable "both"

function cleanup
{
	datasetexists $TESTPOOL/$TESTFS1 && \
		destroy_dataset $TESTPOOL/$TESTFS1 -r
}

log_onexit cleanup

log_assert "'zfs change-key' should promote an encrypted child to an" \
	"encryption root"

log_must eval "echo $PASSPHRASE1 | zfs create -o encryption=on" \
	"-o keyformat=passphrase -o keylocation=prompt $TESTPOOL/$TESTFS1"
log_must zfs create $TESTPOOL/$TESTFS1/child
log_must zfs create -o encryption=off $TESTPOOL/$TESTFS1/child2

log_mustnot eval "echo $PASSPHRASE2 | zfs change-key" \
	"$TESTPOOL/$TESTFS1/child"

log_mustnot eval "echo $PASSPHRASE2 | zfs change-key -o keylocation=prompt" \
	"$TESTPOOL/$TESTFS1/child"

log_must eval "echo $PASSPHRASE2 | zfs change-key -o keyformat=passphrase" \
	"$TESTPOOL/$TESTFS1/child"

log_must zfs unmount $TESTPOOL/$TESTFS1/child
log_must zfs unload-key $TESTPOOL/$TESTFS1/child
log_must key_unavailable $TESTPOOL/$TESTFS1/child

log_must eval "echo $PASSPHRASE2 | zfs load-key $TESTPOOL/$TESTFS1/child"
log_must key_available $TESTPOOL/$TESTFS1/child

log_must zfs destroy $TESTPOOL/$TESTFS1/child
log_must zfs create $TESTPOOL/$TESTFS1/child

log_must eval "echo $PASSPHRASE2 | zfs change-key -o keyformat=passphrase" \
	"-o keylocation=prompt $TESTPOOL/$TESTFS1/child"

log_must zfs unmount $TESTPOOL/$TESTFS1/child
log_must zfs unload-key $TESTPOOL/$TESTFS1/child
log_must key_unavailable $TESTPOOL/$TESTFS1/child

log_must eval "echo $PASSPHRASE2 | zfs load-key $TESTPOOL/$TESTFS1/child"
log_must key_available $TESTPOOL/$TESTFS1/child
log_must zfs unmount $TESTPOOL/$TESTFS1/child2
log_must zfs mount $TESTPOOL/$TESTFS1/child2

log_pass "'zfs change-key' promotes an encrypted child to an encryption root"
