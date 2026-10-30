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
. $STF_SUITE/tests/functional/cli_root/zfs_load-key/zfs_load-key_common.kshlib

#
# DESCRIPTION:
# A raw incremental receive leaves the key of a filesystem alone where the
# encryption root it inherits from holds a wrapping key of its own
# (issue #12614), and hands the sending side's key over where it does not.
#
# STRATEGY:
# 1. Raw send an encrypted filesystem to a pool whose encryption root uses
#    a different passphrase and inherit the key there with 'change-key -i'
# 2. Raw send an incremental snapshot on top of it
# 3. Unload the keys and load them again from the receiving encryption root
# 4. Verify the received filesystem still mounts and holds both files
# 5. Verify a filesystem received as an encryption root of its own does
#    still follow a key change made on the sending side
# 6. Verify a filesystem which inherits from a received encryption root
#    follows that key change as well
#

verify_runnable "both"

function cleanup
{
	log_must cleanup_pool $POOL
	log_must cleanup_pool $POOL2
	log_must cleanup_pool $POOL3
	log_must setup_test_model $POOL
}

log_assert "Raw incremental receives do not overwrite an inherited key"
log_onexit cleanup

log_must cleanup_pool $POOL
log_must cleanup_pool $POOL2
log_must cleanup_pool $POOL3

log_must eval "echo $PASSPHRASE1 | zfs create -o encryption=on" \
	"-o keyformat=passphrase $POOL/send"
log_must zfs create $POOL/send/fs
log_must eval "echo $PASSPHRASE2 | zfs create -o encryption=on" \
	"-o keyformat=passphrase $POOL2/recv"

log_must dd if=/dev/urandom of=/$POOL/send/fs/file1 bs=1M count=1
log_must zfs snapshot -r $POOL/send@snap1

log_must eval "zfs send -w $POOL/send/fs@snap1 | zfs receive $POOL2/recv/fs"

# The filesystem arrives as an encryption root of its own, holding the key
# of the sending side. Hand it over to the receiving one.
log_must verify_encryption_root $POOL2/recv/fs $POOL2/recv/fs
log_must eval "echo $PASSPHRASE1 | zfs load-key $POOL2/recv/fs"
log_must zfs change-key -i $POOL2/recv/fs
log_must verify_encryption_root $POOL2/recv/fs $POOL2/recv

log_must dd if=/dev/urandom of=/$POOL/send/fs/file2 bs=1M count=1
log_must zfs snapshot $POOL/send/fs@snap2

log_must eval "zfs send -w -i @snap1 $POOL/send/fs@snap2 |" \
	"zfs receive $POOL2/recv/fs"
log_must verify_encryption_root $POOL2/recv/fs $POOL2/recv

# The master key stays cached until it is unloaded, so the damage a receive
# used to do to the copy on disk only showed up here.
log_must zfs unmount -u $POOL2/recv
log_must eval "echo $PASSPHRASE2 | zfs mount -l $POOL2/recv"
log_must zfs mount $POOL2/recv/fs

log_must cmp_xxh128 /$POOL/send/fs/file1 /$POOL2/recv/fs/file1
log_must cmp_xxh128 /$POOL/send/fs/file2 /$POOL2/recv/fs/file2

# A filesystem received as an encryption root of its own still follows the
# sending side, key changes included.
log_must eval "zfs send -w $POOL/send@snap1 | zfs receive $POOL2/root"
log_must verify_encryption_root $POOL2/root $POOL2/root
log_must eval "echo $PASSPHRASE1 | zfs load-key $POOL2/root"
log_must zfs unload-key $POOL2/root

log_must eval "echo $PASSPHRASE | zfs change-key $POOL/send"
log_must zfs snapshot $POOL/send@snap3
log_must eval "zfs send -w -i @snap1 $POOL/send@snap3 |" \
	"zfs receive $POOL2/root"

log_mustnot eval "echo $PASSPHRASE1 | zfs load-key $POOL2/root"
log_must eval "echo $PASSPHRASE | zfs load-key $POOL2/root"

# So does one which inherits from a received encryption root: the wrapping
# key there is the sending side's own, so its key has to keep up too.
log_must zfs snapshot -r $POOL/send@tree1
log_must eval "zfs send -Rw $POOL/send@tree1 | zfs receive -d -F $POOL3"
log_must verify_encryption_root $POOL3/send $POOL3/send
log_must verify_encryption_root $POOL3/send/fs $POOL3/send

log_must eval "echo $PASSPHRASE1 | zfs change-key $POOL/send"
log_must zfs snapshot -r $POOL/send@tree2
log_must eval "zfs send -Rw -I @tree1 $POOL/send@tree2 |" \
	"zfs receive -d $POOL3"

log_must eval "echo $PASSPHRASE1 | zfs load-key $POOL3/send"
log_must zfs mount $POOL3/send
log_must zfs mount $POOL3/send/fs
log_must cmp_xxh128 /$POOL/send/fs/file2 /$POOL3/send/fs/file2

log_pass "Raw incremental receives do not overwrite an inherited key"
