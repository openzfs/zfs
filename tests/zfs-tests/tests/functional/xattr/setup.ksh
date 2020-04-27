#!/bin/ksh -p
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
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
	svcs svc:/network/nis/client:default | grep online > /dev/null
	if [ $? -eq 0 ]
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
default_setup $DISK
