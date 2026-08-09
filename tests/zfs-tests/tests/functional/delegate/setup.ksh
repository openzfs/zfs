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
