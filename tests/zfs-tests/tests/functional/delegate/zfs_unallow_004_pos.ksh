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
#	Verify '-s' will remove permissions from the named set.
#
# STRATEGY:
#	1. Set @basic set to $ROOT_TESTFS or $ROOT_TESTVOL and allow @basic
#	   to $STAFF1
#	2. Verify $STAFF1 have @basic permissions.
#	3. Verify '-s' will remove permission from the named set.
#

verify_runnable "both"

log_assert "Verify '-s' will remove permissions from the named set."
log_onexit restore_root_datasets

for dtst in $DATASETS ; do
	log_must zfs allow -s @basic $LOCAL_DESC_SET $dtst
	log_must zfs allow -u $STAFF1 @basic $dtst

	log_must verify_perm $dtst $LOCAL_DESC_SET $STAFF1
	log_must zfs unallow -s @basic $LOCAL_DESC_SET $dtst
	log_must verify_noperm $dtst $LOCAL_DESC_SET $STAFF1
done

log_pass "Verify '-s' will remove permissions from the named set passed."
