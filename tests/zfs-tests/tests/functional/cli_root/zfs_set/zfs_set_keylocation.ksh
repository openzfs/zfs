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
# Unencrypted datasets should only allow keylocation of 'none', encryption
# roots should only allow keylocation of 'prompt' and file URI, and encrypted
# child datasets should not be able to change their keylocation.
#
# STRATEGY:
# 1. Verify the key location of the default dataset is 'none'
# 2. Attempt to change the key location of the default dataset
# 3. Create an encrypted dataset using a key file
# 4. Attempt to change the key location of the encrypted dataset to 'none',
#    an invalid location, its current location, and 'prompt'
# 5. Attempt to reload the encrypted dataset key using the new key location
# 6. Create a encrypted child dataset
# 7. Verify the key location of the child dataset is 'none'
# 8. Attempt to change the key location of the child dataset
# 9. Verify the key location of the child dataset has not changed
#

verify_runnable "both"

function cleanup
{
	datasetexists $TESTPOOL/$TESTFS1 && \
		log_must zfs destroy -r $TESTPOOL/$TESTFS1
}
log_onexit cleanup

log_assert "Key location can only be 'prompt' or a file path for encryption" \
	"roots, and 'none' for unencrypted volumes"

log_must eval "echo $PASSPHRASE > /$TESTPOOL/pkey"

log_must verify_keylocation $TESTPOOL/$TESTFS "none"
log_must zfs set keylocation=none $TESTPOOL/$TESTFS
log_mustnot zfs set keylocation=/$TESTPOOL/pkey $TESTPOOL/$TESTFS
log_mustnot zfs set keylocation=file:///$TESTPOOL/pkey $TESTPOOL/$TESTFS
log_must verify_keylocation $TESTPOOL/$TESTFS "none"

log_must zfs create -o encryption=on -o keyformat=passphrase \
	-o keylocation=file:///$TESTPOOL/pkey $TESTPOOL/$TESTFS1

log_mustnot zfs set keylocation=none $TESTPOOL/$TESTFS1
log_mustnot zfs set keylocation=/$TESTPOOL/pkey $TESTPOOL/$TESTFS1

log_must zfs set keylocation=file:///$TESTPOOL/pkey $TESTPOOL/$TESTFS1
log_must verify_keylocation $TESTPOOL/$TESTFS1 "file:///$TESTPOOL/pkey"

log_must zfs set keylocation=prompt $TESTPOOL/$TESTFS1
log_must verify_keylocation $TESTPOOL/$TESTFS1 "prompt"

log_must zfs unmount $TESTPOOL/$TESTFS1
log_must zfs unload-key $TESTPOOL/$TESTFS1

log_must rm /$TESTPOOL/pkey
log_must eval "echo $PASSPHRASE | zfs load-key $TESTPOOL/$TESTFS1"
log_must zfs mount $TESTPOOL/$TESTFS1

log_must zfs create $TESTPOOL/$TESTFS1/child
log_must verify_keylocation $TESTPOOL/$TESTFS1/child "none"

log_mustnot zfs set keylocation=none $TESTPOOL/$TESTFS1/child
log_mustnot zfs set keylocation=prompt $TESTPOOL/$TESTFS1/child
log_mustnot zfs set keylocation=file:///$TESTPOOL/pkey $TESTPOOL/$TESTFS1/child
log_mustnot zfs set keylocation=/$TESTPOOL/pkey $TESTPOOL/$TESTFS1/child

log_must verify_keylocation $TESTPOOL/$TESTFS1/child "none"

log_pass "Key location can only be 'prompt' or a file path for encryption" \
	"roots, and 'none' for unencrypted volumes"
