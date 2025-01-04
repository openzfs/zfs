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
#

. $STF_SUITE/tests/functional/delegate/delegate_common.kshlib

#
# DESCRIPTION:
#	A non-root user can use 'zfs allow' to delegate permissions that
#	they have, if they also have the 'allow' permission.
#
# STRATEGY:
#	1. Set two set permissions to two datasets locally.
#	2. Verify the non-root user can use 'zfs allow' if they have
#	'allow' permission.
#

verify_runnable "both"

log_assert "Verify non-root user can allow permissions."
log_onexit restore_root_datasets

perms1="snapshot,reservation"
perms2="send,compression,checksum,userprop"
childfs=$ROOT_TESTFS/childfs

log_must zfs create $childfs

for dtst in $DATASETS ; do
	# Delegate local permission to $STAFF1
	log_must zfs allow -l $STAFF1 $perms1 $dtst
	log_must zfs allow -l $STAFF1 allow $dtst

	if [[ $dtst == $ROOT_TESTFS ]]; then
		log_must zfs allow -l $STAFF1 $perms2 $childfs
		# $perms1 is local permission in $ROOT_TESTFS
		log_mustnot user_run $STAFF1 zfs allow $OTHER1 $perms1 $childfs
		log_must verify_noperm $childfs $perms1 $OTHER1
	fi

	# Verify 'allow' give non-privilege user delegated permission.
	log_must user_run $STAFF1 zfs allow -l $OTHER1 $perms1 $dtst
	log_must verify_perm $dtst $perms1 $OTHER1

	# $perms2 was not allowed to $STAFF1, so they do not have
	# permission to delegate permission to other users.
	log_mustnot user_run $STAFF1 zfs allow $OTHER1 $perms2 $dtst
	log_must verify_noperm $dtst $perms2 $OTHER1
done

log_pass "Verify non-root user can allow permissions passed."
