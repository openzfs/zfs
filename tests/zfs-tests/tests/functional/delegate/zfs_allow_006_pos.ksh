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
#	Changing permissions in a set will change what is allowed wherever the
#	set is used.
#
# STRATEGY:
#	1. Set create as set @basic.
#	2. Allow set @basic to $STAFF1 on $ROOT_TESTFS or $ROOT_TESTVOL
#	3. Verify $STAFF1 has create permissions.
#	4. Reset snapshot,allow to $basic
#	5. Verify now $STAFF1 have create,allow,destroy permissions.
#

verify_runnable "both"

log_assert "Changing permissions in a set will change what is allowed " \
	"wherever the set is used."
log_onexit restore_root_datasets

fs1=$ROOT_TESTFS/fs1; fs2=$ROOT_TESTFS/fs2
log_must zfs create $fs1
log_must zfs create $fs2

eval set -A dataset $DATASETS
perms1="snapshot,checksum,reservation"

for dtst in $DATASETS $fs1 $fs2; do
	log_must zfs allow -s @basic $perms1 $dtst
	log_must zfs allow $STAFF1 @basic $dtst
	log_must verify_perm $dtst $perms1 $STAFF1
done

perms2="send,compression,userprop"
for dtst in $DATASETS $fs1 $fs2; do
	log_must zfs allow -s @basic $perms2 $dtst
	log_must verify_perm $dtst ${perms1},${perms2} $STAFF1
done

log_pass "Changing permissions in a set will change what is allowed passed."
