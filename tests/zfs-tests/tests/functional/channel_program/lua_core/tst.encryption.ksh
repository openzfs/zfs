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
# http://www.illumos.org/license/CDDL.
#

#
# Copyright (c) 2021 by Determinate Systems. All rights reserved.
#

. $STF_SUITE/tests/functional/channel_program/channel_common.kshlib

#
# DESCRIPTION:
#       zfs.exists should accurately report whether a dataset exists, and
#       report an error if a dataset is in another pool.

verify_runnable "global"

function cleanup
{
	datasetexists $TESTPOOL/$TESTDATASET && \
	    log_must zfs destroy -R $TESTPOOL/$TESTDATASET
}
log_onexit cleanup

TESTDATASET="channelprogramencryption"

passphrase="password"
log_must eval "echo "$passphrase" | zfs create -o encryption=aes-256-ccm " \
        "-o keyformat=passphrase $TESTPOOL/$TESTDATASET"

log_must_program $TESTPOOL $ZCP_ROOT/lua_core/tst.encryption.zcp \
    $TESTPOOL/$TESTDATASET

log_pass "zfs.get_prop(dataset, ...)  on \"encryption\" and \"encryptionroot\" gives correct results"
