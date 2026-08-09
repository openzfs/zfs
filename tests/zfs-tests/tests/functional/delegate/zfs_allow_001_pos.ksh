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
# 	"everyone" is interpreted as the keyword "everyone" whatever the same
# 	name user or group is existing.
#
# STRATEGY:
#	1. Create user 'everyone'.
#	2. Verify 'everyone' is interpreted as keywords.
#	3. Create group 'everyone'.
#	4. Verify 'everyone' is interpreted as keywords.
#

verify_runnable "both"

function cleanup
{
	if [[ $user_added == "TRUE" ]] ; then
		del_user everyone
	fi
	if [[ $group_added == "TRUE" ]] ; then
		del_group everyone
	fi

	restore_root_datasets
}

log_assert "'everyone' is interpreted as a keyword even if a user " \
	"or group named 'everyone' exists."
log_onexit cleanup

eval set -A dataset $DATASETS
typeset perms="snapshot,reservation,compression,checksum,send,userprop"

log_note "Create a user called 'everyone'."
if ! id everyone > /dev/null 2>&1; then
	user_added="TRUE"
	log_must add_user $STAFF_GROUP everyone
fi
for dtst in $DATASETS ; do
	log_must zfs allow everyone $perms $dtst
	log_must verify_perm $dtst $perms $EVERYONE "everyone"
done
log_must restore_root_datasets
if [[ $user_added == "TRUE" ]]; then
	log_must del_user everyone
fi

log_note "Created a group called 'everyone'."
if ! grep -q '^everyone:' /etc/group; then
	group_added="TRUE"
	log_must add_group everyone
fi

for dtst in $DATASETS ; do
	log_must zfs allow everyone $perms $dtst
	log_must verify_perm $dtst $perms $EVERYONE
done
log_must restore_root_datasets
if [[ $group_added == "TRUE" ]]; then
	log_must del_group everyone
fi

log_pass "everyone is always interpreted as keyword passed."
