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
# 'zfs change-key -if' should cause a dataset to inherit its parent key
# without the key being loaded
#
# STRATEGY:
# 1. Create a parent encrypted dataset
# 2. Create a child dataset
# 3. Create a copy of the parent dataset
# 4. Send a copy of the child to the copy of the parent
# 5. Attempt to force inherit the parent key without the keys being loaded
# 6. Verify the key is inherited
# 7. Load the parent key
# 8. Verify the key is available for parent and child
# 9. Attempt to mount the datasets
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

log_assert "'zfs change-key -if' should cause a dataset to inherit its" \
	"parent key"

log_must eval "echo $PASSPHRASE | zfs create -o encryption=on" \
	"-o keyformat=passphrase -o keylocation=prompt $TESTPOOL/$TESTFS1"
log_must eval "echo $PASSPHRASE1 | zfs create $TESTPOOL/$TESTFS1/child"
log_must verify_encryption_root $TESTPOOL/$TESTFS1/child "$TESTPOOL/$TESTFS1"

log_must zfs snapshot -r $TESTPOOL/$TESTFS1@snap

log_must eval "zfs send -w $TESTPOOL/$TESTFS1@snap | zfs receive $TESTPOOL/$TESTFS2"
log_must verify_encryption_root $TESTPOOL/$TESTFS2 $TESTPOOL/$TESTFS2
log_must key_unavailable $TESTPOOL/$TESTFS2

log_must eval "zfs send -w $TESTPOOL/$TESTFS1/child@snap | zfs receive $TESTPOOL/$TESTFS2/child"
log_must verify_encryption_root $TESTPOOL/$TESTFS2/child $TESTPOOL/$TESTFS2/child
log_must key_unavailable $TESTPOOL/$TESTFS2/child

log_must_not zfs change-key -i $TESTPOOL/$TESTFS2/child
log_must_not zfs change-key -f $TESTPOOL/$TESTFS2/child
log_must zfs change-key -if $TESTPOOL/$TESTFS2/child
log_must verify_encryption_root $TESTPOOL/$TESTFS2/child "$TESTPOOL/$TESTFS2"

log_must key_unavailable $TESTPOOL/$TESTFS2
log_must key_unavailable $TESTPOOL/$TESTFS2/child

log_must eval "echo $PASSPHRASE | zfs load-key $TESTPOOL/$TESTFS2"

log_must key_available $TESTPOOL/$TESTFS2
log_must key_available $TESTPOOL/$TESTFS2/child

log_must zfs mount $TESTPOOL/$TESTFS2
log_must zfs mount $TESTPOOL/$TESTFS2/child

log_pass "'zfs change-key -if' causes a dataset to inherit its parent key"