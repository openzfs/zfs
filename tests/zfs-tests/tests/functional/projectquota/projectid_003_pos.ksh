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
# Copyright (c) 2017 by Fan Yong. Fan rights reserved.
#

. $STF_SUITE/tests/functional/projectquota/projectquota_common.kshlib

#
# DESCRIPTION:
#	Check changing project ID for the file with directory-based
#	extended attributes.
#
#
# STRATEGY:
#	1. create new file with default project ID
#	2. set non-ACL extended attributes on the file
#	3. use zfs projectspace to check the object usage
#	4. change the file's project ID
#	5. use zfs projectspace to check the object usage again
#

function cleanup
{
	log_must rm -f $PRJGUARD
	log_must rm -f $PRJFILE
}

if ! lsattr -pd > /dev/null 2>&1; then
	log_unsupported "Current e2fsprogs does not support set/show project ID"
fi

log_onexit cleanup

log_assert "Check changing project ID with directory-based extended attributes"

log_must zfs set xattr=dir $QFS

log_must touch $PRJGUARD
log_must chattr -p $PRJID1 $PRJGUARD
log_must touch $PRJFILE
log_must setfattr -n trusted.ea1 -v val1 $PRJFILE
log_must setfattr -n trusted.ea2 -v val2 $PRJFILE
log_must setfattr -n trusted.ea3 -v val3 $PRJFILE

sync_pool
typeset prj_bef=$(project_obj_count $QFS $PRJID1)

log_must chattr -p $PRJID1 $PRJFILE
sync_pool
typeset prj_aft=$(project_obj_count $QFS $PRJID1)

[[ $prj_aft -ge $((prj_bef + 5)) ]] ||
	log_fail "new value ($prj_aft) is NOT 5 larger than old one ($prj_bef)"

log_pass "Changing project ID with directory-based extended attributes pass"
