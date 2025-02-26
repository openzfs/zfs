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
# Copyright (c) 2017 Datto, Inc. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zfs_load-key/zfs_load-key_common.kshlib

#
# DESCRIPTION:
# 'zfs rename' should be able to move an unencrypted dataset to a child
# of an encrypted dataset
#
# STRATEGY:
# 1. Create an encrypted dataset
# 2. Rename the default dataset to a child of the encrypted dataset
# 3. Confirm the child dataset doesn't have any encryption properties
#

verify_runnable "both"

function cleanup
{
	datasetexists $TESTPOOL/$TESTFS2 && \
		destroy_dataset $TESTPOOL/$TESTFS2 -r
}
log_onexit cleanup

log_assert "'zfs rename' should allow renaming an unencrypted dataset to a" \
	"child of an encrypted dataset"

log_must eval "echo $PASSPHRASE | zfs create -o encryption=on" \
	"-o keyformat=passphrase -o keylocation=prompt $TESTPOOL/$TESTFS2"
log_must zfs rename $TESTPOOL/$TESTFS $TESTPOOL/$TESTFS2/$TESTFS
log_must test "$(get_prop 'encryption' $TESTPOOL/$TESTFS2/$TESTFS)" == "off"

log_pass "'zfs rename' allows renaming an unencrypted dataset to a child" \
	"of an encrypted dataset"
