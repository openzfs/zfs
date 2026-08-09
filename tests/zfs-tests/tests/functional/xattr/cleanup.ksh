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
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/xattr/xattr_common.kshlib

USES_NIS=$(<$TEST_BASE_DIR/zfs-xattr-test-nis.txt)
rm $TEST_BASE_DIR/zfs-xattr-test-nis.txt $TEST_BASE_DIR/zfs-xattr-test-user.txt

if [ "${USES_NIS}" == "true" ]
then
    svcadm enable svc:/network/nis/client:default
fi

default_cleanup_noexit

del_user $ZFS_USER
del_group $ZFS_GROUP

log_pass
