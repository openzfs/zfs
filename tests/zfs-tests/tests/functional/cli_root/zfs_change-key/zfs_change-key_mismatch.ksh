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
# STRATEGY:
# 1. Create two encrypted datasets, each its own encryption root
# 2. Raw send a child of the first one to the second one
# 3. Load the received dataset's key and inherit the second root's key
# 4. Raw send an incremental stream to it, which restores the sending
#    root's key material while it still points at the local root
# 5. Verify 'zfs change-key' on the dataset fails
# 6. Verify 'zfs change-key' on its encryption root fails
# 7. Verify the encryption root can be rekeyed once the dataset is gone
#

verify_runnable "both"

function cleanup
{
	datasetexists $TESTPOOL/$TESTFS1 && \
		destroy_dataset $TESTPOOL/$TESTFS1 -r
	datasetexists $TESTPOOL/$TESTFS2 && \
		destroy_dataset $TESTPOOL/$TESTFS2 -r
}
log_onexit cleanup

log_assert "'zfs change-key' should fail on a dataset whose key material" \
	"does not match its encryption root"

log_must eval "echo $PASSPHRASE | zfs create -o encryption=on" \
	"-o keyformat=passphrase -o keylocation=prompt $TESTPOOL/$TESTFS1"
log_must eval "echo $PASSPHRASE1 | zfs create -o encryption=on" \
	"-o keyformat=passphrase -o keylocation=prompt $TESTPOOL/$TESTFS2"

log_must zfs create $TESTPOOL/$TESTFS1/child
log_must zfs snapshot $TESTPOOL/$TESTFS1/child@snap1
log_must eval "zfs send -w $TESTPOOL/$TESTFS1/child@snap1 |" \
	"zfs receive -u $TESTPOOL/$TESTFS2/child"

log_must eval "echo $PASSPHRASE | zfs load-key $TESTPOOL/$TESTFS2/child"
log_must zfs change-key -i $TESTPOOL/$TESTFS2/child
log_must verify_encryption_root $TESTPOOL/$TESTFS2/child "$TESTPOOL/$TESTFS2"

log_must zfs snapshot $TESTPOOL/$TESTFS1/child@snap2
log_must eval "zfs send -w -i $TESTPOOL/$TESTFS1/child@snap1" \
	"$TESTPOOL/$TESTFS1/child@snap2 |" \
	"zfs receive -u $TESTPOOL/$TESTFS2/child"

log_mustnot eval "echo $PASSPHRASE2 | zfs change-key -o keyformat=passphrase" \
	"-o keylocation=prompt $TESTPOOL/$TESTFS2/child"
log_mustnot eval "echo $PASSPHRASE2 | zfs change-key -o keyformat=passphrase" \
	"-o keylocation=prompt $TESTPOOL/$TESTFS2"

log_must destroy_dataset $TESTPOOL/$TESTFS2/child -r
log_must eval "echo $PASSPHRASE2 | zfs change-key -o keyformat=passphrase" \
	"-o keylocation=prompt $TESTPOOL/$TESTFS2"

log_pass "'zfs change-key' fails on a dataset whose key material does not" \
	"match its encryption root"
