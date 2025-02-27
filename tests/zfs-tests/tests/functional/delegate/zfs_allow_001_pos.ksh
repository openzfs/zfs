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
