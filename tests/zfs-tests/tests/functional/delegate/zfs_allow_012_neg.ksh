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
