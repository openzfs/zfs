#!/bin/ksh -p
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or https://opensource.org/licenses/CDDL-1.0.
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

#
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/acl/acl_common.kshlib

log_must getfacl --version
log_must setfacl --version

cleanup_user_group

# Create staff group and add user to it
log_must add_group $ZFS_ACL_STAFF_GROUP
log_must add_user $ZFS_ACL_STAFF_GROUP $ZFS_ACL_STAFF1

DISK=${DISKS%% *}
default_setup_noexit $DISK
log_must chmod 777 $TESTDIR

# Use POSIX ACLs on filesystem
log_must zfs set acltype=posix $TESTPOOL/$TESTFS
log_must zfs set xattr=sa $TESTPOOL/$TESTFS

log_pass
