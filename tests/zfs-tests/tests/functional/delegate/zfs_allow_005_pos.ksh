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
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/delegate/delegate_common.kshlib

#
# DESCRIPTION:
#	Verify option '-c' will be granted locally to the creator on any
#	newly-created descendent file systems.
#
# STRATEGY:
#	1. Allow create permissions to everyone on $ROOT_TESTFS locally.
#	2. Allow '-c' create to $ROOT_TESTFS.
#	3. chmod 777 the mountpoint of $ROOT_TESTFS
#	4. Verify only creator can create descendent dataset on
#	   $ROOT_TESTFS/$user.
#

verify_runnable "both"

log_assert "Verify option '-c' will be granted locally to the creator."
log_onexit restore_root_datasets

eval set -A dataset $DATASETS
typeset perms="snapshot,reservation,compression,checksum,userprop"

log_must zfs allow -l everyone create,mount $ROOT_TESTFS
log_must zfs allow -c $perms $ROOT_TESTFS

mntpnt=$(get_prop mountpoint $ROOT_TESTFS)
log_must chmod 777 $mntpnt

for user in $EVERYONE; do
	childfs=$ROOT_TESTFS/$user

	user_run $user zfs create $childfs

	for other in $EVERYONE; do
		#
		# Verify only the creator has the $perm time permissions.
		#
		if [[ $other == $user ]]; then
			log_must verify_perm $childfs $perms $user
		else
			log_must verify_noperm $childfs $perms $other
		fi
	done
done

log_pass "Verify option '-c' will be granted locally to the creator passed."
