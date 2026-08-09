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

log_mustnot eval "zfs get all $QFS | grep -w projectquota"
log_mustnot eval "zfs get all $QFS | grep -w projectobjquota"

log_pass "zfs get all will not print out project{obj}quota"
