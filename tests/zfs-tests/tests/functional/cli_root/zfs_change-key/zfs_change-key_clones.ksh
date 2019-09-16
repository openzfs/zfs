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
# 'zfs change-key' should correctly update encryption roots with clones.
#
# STRATEGY:
# 1. Create an encrypted dataset
# 2. Create an encryption root child of the first dataset
# 3. Clone the child encryption root twice
# 4. Add inheriting children to the encryption root and each of the clones
# 5. Verify the encryption roots
# 6. Have the child encryption root inherit from its parent
# 7. Verify the encryption root for all datasets is now the parent dataset
#

verify_runnable "both"

function cleanup
{
	datasetexists $TESTPOOL/$TESTFS1 && \
		log_must zfs destroy -Rf $TESTPOOL/$TESTFS1
}

log_onexit cleanup

log_assert "'zfs change-key' should correctly update encryption " \
	"roots with clones"

log_must eval "echo $PASSPHRASE1 | zfs create -o encryption=on" \
	"-o keyformat=passphrase -o keylocation=prompt $TESTPOOL/$TESTFS1"
log_must eval "echo $PASSPHRASE2 | zfs create -o encryption=on" \
	"-o keyformat=passphrase -o keylocation=prompt $TESTPOOL/$TESTFS1/child"
log_must zfs snapshot $TESTPOOL/$TESTFS1/child@1
log_must zfs clone $TESTPOOL/$TESTFS1/child@1 $TESTPOOL/$TESTFS1/clone1
log_must zfs clone $TESTPOOL/$TESTFS1/child@1 $TESTPOOL/$TESTFS1/clone2
log_must zfs create $TESTPOOL/$TESTFS1/child/A
log_must zfs create $TESTPOOL/$TESTFS1/clone1/B
log_must zfs create $TESTPOOL/$TESTFS1/clone2/C

log_must verify_encryption_root $TESTPOOL/$TESTFS1 $TESTPOOL/$TESTFS1
log_must verify_encryption_root $TESTPOOL/$TESTFS1/child $TESTPOOL/$TESTFS1/child
log_must verify_encryption_root $TESTPOOL/$TESTFS1/clone1 $TESTPOOL/$TESTFS1/child
log_must verify_encryption_root $TESTPOOL/$TESTFS1/clone2 $TESTPOOL/$TESTFS1/child
log_must verify_encryption_root $TESTPOOL/$TESTFS1/child/A $TESTPOOL/$TESTFS1/child
log_must verify_encryption_root $TESTPOOL/$TESTFS1/clone1/B $TESTPOOL/$TESTFS1/child
log_must verify_encryption_root $TESTPOOL/$TESTFS1/clone2/C $TESTPOOL/$TESTFS1/child

log_must zfs change-key -i $TESTPOOL/$TESTFS1/child

log_must verify_encryption_root $TESTPOOL/$TESTFS1 $TESTPOOL/$TESTFS1
log_must verify_encryption_root $TESTPOOL/$TESTFS1/child $TESTPOOL/$TESTFS1
log_must verify_encryption_root $TESTPOOL/$TESTFS1/clone1 $TESTPOOL/$TESTFS1
log_must verify_encryption_root $TESTPOOL/$TESTFS1/clone2 $TESTPOOL/$TESTFS1
log_must verify_encryption_root $TESTPOOL/$TESTFS1/child/A $TESTPOOL/$TESTFS1
log_must verify_encryption_root $TESTPOOL/$TESTFS1/clone1/B $TESTPOOL/$TESTFS1
log_must verify_encryption_root $TESTPOOL/$TESTFS1/clone2/C $TESTPOOL/$TESTFS1

log_pass "'zfs change-key' correctly updates encryption roots with clones"
