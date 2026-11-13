#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# https://opensource.org/license/CDDL-1.0.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zfs_load-key/zfs_load-key_common.kshlib

#
# DESCRIPTION:
# 'zfs change-key' should fail on a dataset whose key material does not
# match the encryption root it points at, rather than panic.
#
# An incremental raw receive into a dataset that inherits its key from a
# local encryption root used to leave it this way.  The receive no longer
# does that for passphrase-keyed roots, but it doesn't repair datasets
# that already are, so the test imports a pool that has one.
#
# blockfiles/key_mismatch.dat.bz2 is a 64M file vdev holding pool
# keymismatch.  keymismatch/dst (passphrase $PASSPHRASE1) is an encryption
# root, and keymismatch/dst/child inherits from it but holds a key wrapped
# with $PASSPHRASE.  It was made with a build from before the receive
# change (3d487ef460):
#	truncate -s 64m key_mismatch.dat
#	zpool create -O mountpoint=none keymismatch $PWD/key_mismatch.dat
#	echo $PASSPHRASE | zfs create -o encryption=on \
#	    -o keyformat=passphrase -o keylocation=prompt keymismatch/src
#	echo $PASSPHRASE1 | zfs create -o encryption=on \
#	    -o keyformat=passphrase -o keylocation=prompt keymismatch/dst
#	zfs create keymismatch/src/child
#	zfs snapshot keymismatch/src/child@snap1
#	zfs send -w keymismatch/src/child@snap1 |
#	    zfs receive -u keymismatch/dst/child
#	echo $PASSPHRASE | zfs load-key keymismatch/dst/child
#	zfs change-key -i keymismatch/dst/child
#	zfs snapshot keymismatch/src/child@snap2
#	zfs send -w -i @snap1 keymismatch/src/child@snap2 |
#	    zfs receive -u keymismatch/dst/child
#	zfs destroy -r keymismatch/src
#	zpool export keymismatch
#	bzip2 -9 key_mismatch.dat
#
# STRATEGY:
# 1. Import the pool and load its encryption root's key
# 2. Verify 'zfs change-key' on the dataset fails with an unwrap error
# 3. Verify 'zfs change-key' on its encryption root fails with an unwrap error
# 4. Verify the encryption root can be rekeyed once the dataset is gone
#

verify_runnable "global"

POOL_NAME=keymismatch
POOL_FILE=key_mismatch.dat

function cleanup
{
	poolexists $POOL_NAME && destroy_pool $POOL_NAME
	rm -f /$TESTPOOL/$POOL_FILE
}
log_onexit cleanup

log_assert "'zfs change-key' should fail on a dataset whose key material" \
	"does not match its encryption root"

log_must eval "bzcat <" \
	"$STF_SUITE/tests/functional/cli_root/zfs_change-key/blockfiles/$POOL_FILE.bz2" \
	">/$TESTPOOL/$POOL_FILE"
log_must zpool import -N -d /$TESTPOOL/$POOL_FILE $POOL_NAME
log_must eval "echo $PASSPHRASE1 | zfs load-key $POOL_NAME/dst"
log_must verify_encryption_root $POOL_NAME/dst/child $POOL_NAME/dst

log_mustnot_expect "cannot be unwrapped" eval "echo $PASSPHRASE2 |" \
	"zfs change-key -o keyformat=passphrase -o keylocation=prompt" \
	"$POOL_NAME/dst/child"
log_mustnot_expect "cannot be unwrapped" eval "echo $PASSPHRASE2 |" \
	"zfs change-key -o keyformat=passphrase -o keylocation=prompt" \
	"$POOL_NAME/dst"

log_must destroy_dataset $POOL_NAME/dst/child -r
log_must eval "echo $PASSPHRASE2 | zfs change-key -o keyformat=passphrase" \
	"-o keylocation=prompt $POOL_NAME/dst"

log_pass "'zfs change-key' fails on a dataset whose key material does not" \
	"match its encryption root"
