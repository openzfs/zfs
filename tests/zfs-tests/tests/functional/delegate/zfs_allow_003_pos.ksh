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
#	Verify option '-l' only allow permission to the dataset itself.
#
# STRATEGY:
#	1. Create descendent datasets of $ROOT_TESTFS
#	2. Select user, group and everyone and set local permission separately.
#	3. Set locally permissions to $ROOT_TESTFS or $ROOT_TESTVOL.
#	4. Verify the permissions are only allow on $ROOT_TESTFS or
#	   $ROOT_TESTVOL.
#

verify_runnable "both"

log_assert "Verify option '-l' only allow permission to the dataset itself."
log_onexit restore_root_datasets

childfs=$ROOT_TESTFS/childfs

eval set -A dataset $DATASETS
typeset perms="snapshot,reservation,compression,checksum,userprop"

log_must zfs create $childfs

for dtst in $DATASETS ; do
	log_must zfs allow -l $STAFF1 $perms $dtst
	log_must verify_perm $dtst $perms $STAFF1
	if [[ $dtst == $ROOT_TESTFS ]] ; then
		log_must verify_noperm $childfs $perms \
			$STAFF1 $STAFF2 $OTHER1 $OTHER2
	fi
done

log_must restore_root_datasets

log_must zfs create $childfs
for dtst in $DATASETS ; do
	log_must zfs allow -l -g $STAFF_GROUP $perms $dtst
	log_must verify_perm $dtst $perms $STAFF1 $STAFF2
	if [[ $dtst == $ROOT_TESTFS ]] ; then
		log_must verify_noperm $childfs $perms \
			$STAFF1 $STAFF2 $OTHER1 $OTHER2
	fi
done

log_must restore_root_datasets

log_must zfs create $childfs
for dtst in $DATASETS ; do
	log_must zfs allow -l -e $perms $dtst
	log_must verify_perm $dtst $perms $STAFF1 $STAFF2 $OTHER1 $OTHER2
	if [[ $dtst == $ROOT_TESTFS ]] ; then
		log_must verify_noperm $childfs $perms \
			$STAFF1 $STAFF2 $OTHER1 $OTHER2
	fi
done

log_must restore_root_datasets

log_pass "Verify option '-l' only allow permission to the dataset itself pass."
