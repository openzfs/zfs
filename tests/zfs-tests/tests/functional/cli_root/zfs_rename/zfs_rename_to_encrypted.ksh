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
# 'zfs rename' should not rename an unencrypted dataset to a child
# of an encrypted dataset
#
# STRATEGY:
# 1. Create an encrypted dataset
# 2. Attempt to rename the default dataset to a child of the encrypted dataset
#

verify_runnable "both"

function cleanup
{
	datasetexists $TESTPOOL/$TESTFS2 && \
		log_must zfs destroy $TESTPOOL/$TESTFS2
}
log_onexit cleanup

log_assert "'zfs rename' should not rename an unencrypted dataset to a" \
	"child of an encrypted dataset"

log_must eval "echo $PASSPHRASE | zfs create -o encryption=on" \
	"-o keyformat=passphrase -o keylocation=prompt $TESTPOOL/$TESTFS2"
log_mustnot zfs rename $TESTPOOL/$TESTFS $TESTPOOL/$TESTFS2/$TESTFS

log_pass "'zfs rename' does not rename an unencrypted dataset to a child" \
	"of an encrypted dataset"
