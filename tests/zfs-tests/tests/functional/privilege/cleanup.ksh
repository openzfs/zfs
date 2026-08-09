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

#
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

if is_linux || is_freebsd; then
	log_unsupported "Privilege tests require pfexec command"
fi

verify_runnable "global"

ZFS_USER=$(<$TEST_BASE_DIR/zfs-privs-test-user.txt)
[[ -z $ZFS_USER ]] && log_fail "no ZFS_USER found"

USES_NIS=$(<$TEST_BASE_DIR/zfs-privs-test-nis.txt)

if [ "${USES_NIS}" == "true" ]
then
    svcadm enable svc:/network/nis/client:default
fi

userdel $ZFS_USER
[[ -d /export/home/$ZFS_USER ]] && rm -rf /export/home/$ZFS_USER
rm $TEST_BASE_DIR/zfs-privs-test-nis.txt
rm $TEST_BASE_DIR/zfs-privs-test-user.txt

default_cleanup
