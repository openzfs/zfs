#!/bin/ksh -p
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
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
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2017 by Fan Yong. All rights reserved.
#

. $STF_SUITE/tests/functional/projectquota/projectquota_common.kshlib

#
# DESCRIPTION:
#	The project{obj}quota will not change during zfs actions, such as
#	snapshot,clone,rename,upgrade,send,receive.
#
#
# STRATEGY:
#	1. Create a pool, and create fs with preset project{obj}quota
#	2. Check set project{obj}quota via zfs snapshot|clone|list -o
#	3. Check the project{obj}quota can not change during zfs
#	   rename|upgrade|promote
#	4. Check the project{obj}quota can not change during zfs clone
#	5. Check the project{obj}quota can not change during zfs send/receive
#

function cleanup
{
	for ds in $TESTPOOL/fs $TESTPOOL/fs-rename $TESTPOOL/fs-clone; do
		datasetexists $ds && destroy_dataset $ds -rRf
	done
}

log_onexit cleanup

log_assert "the project{obj}quota can't change during zfs actions"

cleanup

log_must zfs create -o projectquota@$PRJID1=$PQUOTA_LIMIT \
	-o projectobjquota@$PRJID2=$PQUOTA_OBJLIMIT $TESTPOOL/fs

log_must zfs snapshot $TESTPOOL/fs@snap
log_must eval "zfs list -r -o projectquota@$PRJID1,projectobjquota@$PRJID2 \
	$TESTPOOL >/dev/null 2>&1"

log_must check_quota "projectquota@$PRJID1" $TESTPOOL/fs@snap "$PQUOTA_LIMIT"
log_must check_quota "projectobjquota@$PRJID2" $TESTPOOL/fs@snap \
	"$PQUOTA_OBJLIMIT"


log_note "clone fs gets its parent's project{obj}quota initially"
log_must zfs clone  -o projectquota@$PRJID1=$PQUOTA_LIMIT \
		-o projectobjquota@$PRJID2=$PQUOTA_OBJLIMIT \
		$TESTPOOL/fs@snap $TESTPOOL/fs-clone

log_must eval "zfs list -r -o projectquota@$PRJID1,projectobjquota@$PRJID2 \
	$TESTPOOL >/dev/null 2>&1"

log_must check_quota "projectquota@$PRJID1" $TESTPOOL/fs-clone "$PQUOTA_LIMIT"
log_must check_quota "projectobjquota@$PRJID2" $TESTPOOL/fs-clone \
	"$PQUOTA_OBJLIMIT"

log_must eval "zfs list -o projectquota@$PRJID1,projectobjquota@$PRJID2 \
	$TESTPOOL/fs-clone >/dev/null 2>&1"

log_note "zfs promote can not change the previously set project{obj}quota"
log_must zfs promote $TESTPOOL/fs-clone

log_must eval "zfs list -r -o projectquota@$PRJID1,projectobjquota@$PRJID2 \
	$TESTPOOL >/dev/null 2>&1"

log_must check_quota "projectquota@$PRJID1" $TESTPOOL/fs-clone "$PQUOTA_LIMIT"
log_must check_quota "projectobjquota@$PRJID2" $TESTPOOL/fs-clone \
	"$PQUOTA_OBJLIMIT"

log_note "zfs send receive can not change the previously set project{obj}quota"
log_must zfs send $TESTPOOL/fs-clone@snap | zfs receive $TESTPOOL/fs-rev

log_must eval "zfs list -r -o projectquota@$PRJID1,projectobjquota@$PRJID2 \
	$TESTPOOL >/dev/null 2>&1"

log_must check_quota "projectquota@$PRJID1" $TESTPOOL/fs-rev "$PQUOTA_LIMIT"
log_must check_quota "projectobjquota@$PRJID2" $TESTPOOL/fs-rev \
	"$PQUOTA_OBJLIMIT"

log_note "zfs rename can not change the previously set project{obj}quota"
log_must zfs rename $TESTPOOL/fs-rev $TESTPOOL/fs-rename

log_must eval "zfs list -r -o projectquota@$PRJID1,projectobjquota@$PRJID2 \
	$TESTPOOL  >/dev/null 2>&1"

log_must check_quota "projectquota@$PRJID1" $TESTPOOL/fs-rename "$PQUOTA_LIMIT"
log_must check_quota "projectobjquota@$PRJID2" $TESTPOOL/fs-rename \
	"$PQUOTA_OBJLIMIT"

log_note "zfs upgrade can not change the previously set project{obj}quota"
log_must zfs upgrade $TESTPOOL/fs-rename

log_must eval "zfs list -r -o projectquota@$PRJID1,projectobjquota@$PRJID2 \
	$TESTPOOL >/dev/null 2>&1"

log_must check_quota "projectquota@$PRJID1" $TESTPOOL/fs-rename "$PQUOTA_LIMIT"
log_must check_quota "projectobjquota@$PRJID2" $TESTPOOL/fs-rename \
	"$PQUOTA_OBJLIMIT"

log_pass "the project{obj}quota can't change during zfs actions"
