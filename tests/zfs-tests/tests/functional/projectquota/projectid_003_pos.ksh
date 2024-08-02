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
