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
# Copyright 2026 Oxide Computer Company
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zfs_load-key/zfs_load-key_common.kshlib

#
# DESCRIPTION:
# 'zfs change-key -o user:prop=val' should set a user property while changing
# or inheriting the key.
#
# STRATEGY:
# 1. Create a parent encrypted dataset
# 2. Create a child dataset as an encryption root
# 3. Change parent key while setting a user property
# 4. Verify the user property is set on the parent
# 5. Make the child inherit the parent's key while setting a user property
# 6. Verify the user property is set on the child
#

verify_runnable "both"

function cleanup
{
	datasetexists $TESTPOOL/$TESTFS1 && \
		log_must zfs destroy -r $TESTPOOL/$TESTFS1
}
log_onexit cleanup

log_assert "'zfs change-key -o user:prop=value' should set a user property"

log_must eval "echo $PASSPHRASE | zfs create -o encryption=on" \
	"-o keyformat=passphrase -o keylocation=prompt $TESTPOOL/$TESTFS1"
log_must eval "echo $PASSPHRASE1 | zfs create -o encryption=on" \
	"-o keyformat=passphrase -o keylocation=prompt" \
	"$TESTPOOL/$TESTFS1/child"

log_must verify_encryption_root $TESTPOOL/$TESTFS1/child \
	"$TESTPOOL/$TESTFS1/child"

log_must eval "echo $PASSPHRASE2 | zfs change-key -o user:prop=parentvalue" \
	"$TESTPOOL/$TESTFS1"
log_must eval "zfs get -H -o value user:prop $TESTPOOL/$TESTFS1 | \
	grep -q parentvalue"

log_must zfs change-key -i -o user:prop=abcd -o user:prop2=efgh \
	$TESTPOOL/$TESTFS1/child
log_must verify_encryption_root $TESTPOOL/$TESTFS1/child "$TESTPOOL/$TESTFS1"
log_must eval "zfs get -H -o value user:prop $TESTPOOL/$TESTFS1/child | \
	grep -q abcd"
log_must eval "zfs get -H -o value user:prop2 $TESTPOOL/$TESTFS1/child | \
	grep -q efgh"

log_pass "'zfs change-key -o user:prop=value' sets a user property"
