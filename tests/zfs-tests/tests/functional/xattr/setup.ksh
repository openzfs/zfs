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

# if we're running NIS, turn it off until we clean up
# (it can cause useradd to take a long time, hitting our TIMEOUT)
if is_illumos; then
	USES_NIS=false
	if svcs svc:/network/nis/client:default | grep -q online
	then
		svcadm disable -t svc:/network/nis/client:default
		USES_NIS=true
	fi
else
	USES_NIS=false
fi

# Make sure we use a brand new user for this
log_must add_group $ZFS_GROUP
log_must add_user $ZFS_GROUP $ZFS_USER

echo $ZFS_USER > $TEST_BASE_DIR/zfs-xattr-test-user.txt
echo $USES_NIS > $TEST_BASE_DIR/zfs-xattr-test-nis.txt

DISK=${DISKS%% *}
default_setup_noexit $DISK
log_must zfs set compression=off $TESTPOOL
log_pass "Setup complete"
