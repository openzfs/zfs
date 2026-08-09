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
# Portions Copyright (c) 2021 iXsystems, Inc.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/acl/acl_common.kshlib

DISK=${DISKS%% *}

cleanup_user_group

# Create staff group and add users to it
log_must add_group $ZFS_ACL_STAFF_GROUP
log_must add_user $ZFS_ACL_STAFF_GROUP $ZFS_ACL_STAFF1
log_must add_user $ZFS_ACL_STAFF_GROUP $ZFS_ACL_STAFF2

default_setup_noexit $DISK

log_must zfs set acltype=off $TESTPOOL/$TESTFS
log_must chmod 0777 $TESTDIR

log_pass
