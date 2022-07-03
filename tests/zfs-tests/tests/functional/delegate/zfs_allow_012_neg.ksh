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
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/delegate/delegate_common.kshlib

#
# DESCRIPTION:
#	Scan all permissions one by one to verify privileged user
#	can not use permissions properly when delegation property is set off
#
# STRATEGY:
#	1. Delegate all the permission one by one to user on dataset.
#	2. Verify privileged user can not use permissions properly when
#	delegation property is off
#

verify_runnable "global"

function cleanup
{
	log_must zpool set delegation=on $TESTPOOL
	log_must restore_root_datasets
}

log_assert "Verify privileged user can not use permissions properly when " \
	"delegation property is set off"
log_onexit cleanup


if is_linux; then
set -A perms	create snapshot mount send allow quota reservation \
		recordsize mountpoint checksum compression canmount atime \
		devices exec volsize setuid readonly snapdir userprop \
		rollback clone rename promote dnodesize \
		zoned xattr receive destroy
elif is_freebsd; then
set -A perms	create snapshot mount send allow quota reservation \
		recordsize mountpoint checksum compression canmount atime \
		devices exec volsize setuid readonly snapdir userprop \
		aclmode aclinherit rollback clone rename promote dnodesize \
		jailed receive destroy
else
set -A perms	create snapshot mount send allow quota reservation \
		recordsize mountpoint checksum compression canmount atime \
		devices exec volsize setuid readonly snapdir userprop \
		aclmode aclinherit rollback clone rename promote dnodesize \
		zoned xattr receive destroy sharenfs share
fi

log_must zpool set delegation=off $TESTPOOL

for dtst in $DATASETS; do
	typeset -i i=0
	while (( i < ${#perms[@]} )); do

		log_must zfs allow $STAFF1 ${perms[$i]} $dtst
		log_must verify_noperm $dtst ${perms[$i]} $STAFF1

		log_must restore_root_datasets
		((i += 1))
	done
done

log_pass "Verify privileged user can not use permissions properly when " \
	"delegation property is set off"
