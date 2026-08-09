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

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/userquota/userquota_common.kshlib

#
# DESCRIPTION:
#       the defaultuserquota and defaultgroupquota will not change during zfs actions, such as
#	snapshot,clone,rename,upgrade,send,receive.
#
#
# STRATEGY:
#       1. Create a pool, and create fs with preset default user,group quota
#       2. Check set default user|group quota via zfs snapshot|clone|list -o
#       3. Check the default user|group quota can not change during zfs rename|upgrade|promote
#       4. Check the default user|group quota can not change during zfs clone
#       5. Check the default user|group quota can not change during zfs send/receive
#

function cleanup
{
	for ds in $TESTPOOL/fs $TESTPOOL/fs-rename $TESTPOOL/fs-clone; do
		datasetexists $ds && destroy_dataset $ds -rRf
	done
}

log_onexit cleanup

log_assert \
	"the default{user|group}quota don't change during zfs actions"

cleanup

log_must zfs create \
	-o defaultuserquota=$UQUOTA_SIZE \
	-o defaultgroupquota=$GQUOTA_SIZE \
	$TESTPOOL/fs

log_must zfs snapshot $TESTPOOL/fs@snap
log_must eval "zfs list -r \
	-o defaultuserquota,defaultgroupquota \
	$TESTPOOL >/dev/null 2>&1"

log_must check_quota "defaultuserquota" $TESTPOOL/fs@snap "$UQUOTA_SIZE"
log_must check_quota "defaultgroupquota" $TESTPOOL/fs@snap "$GQUOTA_SIZE"

log_note "clone fs gets its parent's default{user|group}quota initially"
log_must zfs clone \
	$TESTPOOL/fs@snap $TESTPOOL/fs-clone

log_must eval "zfs list -r \
	-o defaultuserquota,defaultgroupquota \
	$TESTPOOL >/dev/null 2>&1"

log_must check_quota "defaultuserquota" $TESTPOOL/fs-clone "$UQUOTA_SIZE"
log_must check_quota "defaultgroupquota" $TESTPOOL/fs-clone "$GQUOTA_SIZE"

log_must eval "zfs list \
	-o defaultuserquota,defaultgroupquota \
	$TESTPOOL/fs-clone >/dev/null 2>&1"

log_note "zfs promote can not change the previously set default{user|group}quota"
log_must zfs promote $TESTPOOL/fs-clone

log_must eval "zfs list -r \
	-o defaultuserquota,defaultgroupquota \
	$TESTPOOL >/dev/null 2>&1"

log_must check_quota "defaultuserquota" $TESTPOOL/fs-clone "$UQUOTA_SIZE"
log_must check_quota "defaultgroupquota" $TESTPOOL/fs-clone "$GQUOTA_SIZE"

log_note "zfs send receive can not change the previously set default{user|group}quota"
log_must zfs send $TESTPOOL/fs-clone@snap | zfs receive $TESTPOOL/fs-rev

log_must eval "zfs list -r \
	-o defaultuserquota,defaultgroupquota \
	$TESTPOOL >/dev/null 2>&1"

log_must check_quota "defaultuserquota" $TESTPOOL/fs-rev "$UQUOTA_SIZE"
log_must check_quota "defaultgroupquota" $TESTPOOL/fs-rev "$GQUOTA_SIZE"

log_note "zfs rename can not change the previously set default{user|group}quota"
log_must zfs rename $TESTPOOL/fs-rev $TESTPOOL/fs-rename

log_must eval "zfs list -r \
	-o defaultuserquota,defaultgroupquota \
	$TESTPOOL  >/dev/null 2>&1"

log_must check_quota "defaultuserquota" $TESTPOOL/fs-rename "$UQUOTA_SIZE"
log_must check_quota "defaultgroupquota" $TESTPOOL/fs-rename "$GQUOTA_SIZE"

log_note "zfs upgrade can not change the previously set default{user|group}quota"
log_must zfs upgrade $TESTPOOL/fs-rename

log_must eval "zfs list -r \
	-o defaultuserquota,defaultgroupquota \
	$TESTPOOL >/dev/null 2>&1"

log_must check_quota "defaultuserquota" $TESTPOOL/fs-rename "$UQUOTA_SIZE"
log_must check_quota "defaultgroupquota" $TESTPOOL/fs-rename "$GQUOTA_SIZE"

log_pass \
	"the default{user|group}quota don't change during zfs actions"
