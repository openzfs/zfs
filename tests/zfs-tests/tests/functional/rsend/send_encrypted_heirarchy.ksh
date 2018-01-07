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
# Copyright (c) 2017 by Datto Inc. All rights reserved.
#

. $STF_SUITE/tests/functional/rsend/rsend.kshlib
. $STF_SUITE/tests/functional/cli_root/zfs_load-key/zfs_load-key_common.kshlib

#
# DESCRIPTION:
# Raw recursive sends preserve filesystem structure.
#
# STRATEGY:
# 1. Create an encrypted filesystem with a clone and a child
# 2. Snapshot and send the filesystem tree
# 3. Verify that the filesystem structure was correctly received
# 4. Change the child to an encryption root and promote the clone
# 5. Snapshot and send the filesystem tree again
# 6. Verify that the new structure is received correctly
#

verify_runnable "both"

function cleanup
{
	log_must cleanup_pool $POOL
	log_must cleanup_pool $POOL2
	log_must setup_test_model $POOL
}

log_assert "Raw recursive sends preserve filesystem structure."
log_onexit cleanup

# Create the filesystem heirarchy
log_must cleanup_pool $POOL
log_must eval "echo $PASSPHRASE | zfs create -o encryption=on" \
	"-o keyformat=passphrase $POOL/$FS"
log_must zfs snapshot $POOL/$FS@snap
log_must zfs clone $POOL/$FS@snap $POOL/clone
log_must zfs create $POOL/$FS/child

# Back up the tree and verify the structure
log_must zfs snapshot -r $POOL@before
log_must eval "zfs send -wR $POOL@before > $BACKDIR/fs-before-R"
log_must eval "zfs receive -d -F $POOL2 < $BACKDIR/fs-before-R"
dstds=$(get_dst_ds $POOL/$FS $POOL2)
log_must cmp_ds_subs $POOL/$FS $dstds

log_must verify_encryption_root $POOL/$FS $POOL/$FS
log_must verify_keylocation $POOL/$FS "prompt"
log_must verify_origin $POOL/$FS "-"

log_must verify_encryption_root $POOL/clone $POOL/$FS
log_must verify_keylocation $POOL/clone "none"
log_must verify_origin $POOL/clone "$POOL/$FS@snap"

log_must verify_encryption_root $POOL/$FS/child $POOL/$FS
log_must verify_keylocation $POOL/$FS/child "none"

# Alter the heirarchy and re-send
log_must eval "echo $PASSPHRASE1 | zfs change-key -o keyformat=passphrase" \
	"$POOL/$FS/child"
log_must zfs promote $POOL/clone
log_must zfs snapshot -r $POOL@after
log_must eval "zfs send -wR -i $POOL@before $POOL@after >" \
	"$BACKDIR/fs-after-R"
log_must eval "zfs receive -d -F $POOL2 < $BACKDIR/fs-after-R"
log_must cmp_ds_subs $POOL/$FS $dstds

log_must verify_encryption_root $POOL/$FS $POOL/clone
log_must verify_keylocation $POOL/$FS "none"
log_must verify_origin $POOL/$FS "$POOL/clone@snap"

log_must verify_encryption_root $POOL/clone $POOL/clone
log_must verify_keylocation $POOL/clone "prompt"
log_must verify_origin $POOL/clone "-"

log_must verify_encryption_root $POOL/$FS/child $POOL/$FS/child
log_must verify_keylocation $POOL/$FS/child "prompt"

log_pass "Raw recursive sends preserve filesystem structure."
