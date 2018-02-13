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
#	zfs get all <fs> does not print out project{obj}quota
#
# STRATEGY:
#	1. set project{obj}quota to a fs
#	2. check zfs get all fs
#

function cleanup
{
	log_must cleanup_projectquota
}

log_onexit cleanup

log_assert "Check zfs get all will not print out project{obj}quota"

log_must zfs set projectquota@$PRJID1=50m $QFS
log_must zfs set projectobjquota@$PRJID2=100 $QFS

log_mustnot eval "zfs get all $QFS | grep projectquota"
log_mustnot eval "zfs get all $QFS | grep projectobjquota"

log_pass "zfs get all will not print out project{obj}quota"
