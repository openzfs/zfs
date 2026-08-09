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
#	Verify '-d' only remove the permissions on descendent filesystem.
#
# STRATEGY:
#	1. Set up unallow test model.
#	2. Implement unallow -d to $ROOT_TESTFS
#	3. Verify '-d' only remove the permissions on descendent filesystem.
#

verify_runnable "both"

log_assert "Verify '-d' only removed the descendent permissions."
log_onexit restore_root_datasets

log_must setup_unallow_testenv

log_must zfs unallow -d $STAFF2 $ROOT_TESTFS
log_must verify_noperm $SUBFS $DESC_SET $STAFF2

log_must zfs unallow -d $OTHER1 $ROOT_TESTFS
log_must verify_noperm $SUBFS $LOCAL_DESC_SET $OTHER1
log_must verify_perm $ROOT_TESTFS $LOCAL_DESC_SET $OTHER1

log_must verify_perm $ROOT_TESTFS $LOCAL_DESC_SET $OTHER2
log_must verify_perm $SUBFS $LOCAL_DESC_SET $OTHER2

log_pass "Verify '-d' only removed the descendent permissions passed"
