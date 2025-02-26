#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
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
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
# Copyright (c) 2018 George Melikov. All Rights Reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/delegate/delegate_common.kshlib

if is_illumos; then
	# check svc:/network/nis/client:default state
	# disable it if the state is ON
	# and the state will be restored during cleanup.ksh
	log_must rm -f $NISSTAFILE
	if [[ "ON" == $(svcs -H -o sta svc:/network/nis/client:default) ]]; then
		log_must svcadm disable -t svc:/network/nis/client:default
		log_must touch $NISSTAFILE
	fi
fi

if is_freebsd; then
	# To pass user mount tests
	log_must sysctl vfs.usermount=1
fi

cleanup_user_group

# Create staff group and add two user to it
log_must add_group $STAFF_GROUP
log_must add_user $STAFF_GROUP $STAFF1
log_must add_user $STAFF_GROUP $STAFF2

# Create other group and add two user to it
log_must add_group $OTHER_GROUP
log_must add_user $OTHER_GROUP $OTHER1
log_must add_user $OTHER_GROUP $OTHER2

#
# Verify the test user can execute the zfs utilities.  This may not
# be possible due to default permissions on the user home directory.
# This can be resolved granting group read access.
#
# chmod 0750 $HOME
#
user_run $STAFF1 zfs list ||
	log_unsupported "Test user $STAFF1 cannot execute zfs utilities"

DISK=${DISKS%% *}

if is_linux; then
	log_must set_tunable64 ADMIN_SNAPSHOT 1
fi

default_volume_setup $DISK
