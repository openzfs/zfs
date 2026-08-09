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
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/delegate/delegate_common.kshlib

#
# DESCRIPTION:
# <user|group> argument is interpreted as a user if possible, then as a group as
# possible.
#
# STRATEGY:
#	1. Create user $STAFF_GROUP
#	2. Delegate permissions to $STAFF_GROUP
#	3. Verify user $STAFF_GROUP has the permissions.
#	4. Delete user $STAFF_GROUP and allow the permission to $STAFF_GROUP
#	5. Verify $STAFF_GROUP is interpreted as group.
#

verify_runnable "both"

function cleanup
{
	if id $STAFF_GROUP > /dev/null 2>&1; then
		log_must del_user $STAFF_GROUP
		if is_freebsd; then
			# pw userdel also deletes the group with the same name
			# and has no way to opt out of this behavior (yet).
			# Recreate the group as a workaround.
			log_must add_group $STAFF_GROUP
			log_must add_user $STAFF_GROUP $STAFF1
			log_must add_user $STAFF_GROUP $STAFF2
		fi
	fi

	restore_root_datasets
}

log_assert "<user|group> is interpreted as user if possible, then as group."
log_onexit cleanup

eval set -A dataset $DATASETS
typeset perms="snapshot,reservation,compression,checksum,send,userprop"

log_must add_user $STAFF_GROUP $STAFF_GROUP
for dtst in $DATASETS ; do
	log_must zfs allow $STAFF_GROUP $perms $dtst
	log_must verify_perm $dtst $perms $STAFF_GROUP
	log_must verify_noperm $dtst $perms $STAFF1 $STAFF2
done

log_must restore_root_datasets

log_must del_user $STAFF_GROUP
if is_freebsd; then
	# pw userdel also deletes the group with the same name
	# and has no way to opt out of this behavior (yet).
	# Recreate the group as a workaround.
	log_must add_group $STAFF_GROUP
	log_must add_user $STAFF_GROUP $STAFF1
	log_must add_user $STAFF_GROUP $STAFF2
fi
for dtst in $datasets ; do
	log_must zfs allow $STAFF_GROUP $perms $dtst
	log_must verify_perm $dtst $perms $STAFF1 $STAFF2
done

log_pass "<user|group> is interpreted as user if possible, then as group passed."
