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
#	Verify '-l' only removed the local permissions.
#
# STRATEGY:
#	1. Set up unallow test model.
#	2. Implement unallow -l to $ROOT_TESTFS or $TESTVOL
#	3. Verify '-l' only remove the local permissions.
#

verify_runnable "both"

log_assert "Verify '-l' only removed the local permissions."
log_onexit restore_root_datasets

log_must setup_unallow_testenv

for dtst in $DATASETS ; do
	log_must zfs unallow -l $STAFF1 $dtst
	log_must verify_noperm $dtst $LOCAL_SET $STAFF1

	log_must zfs unallow -l $OTHER1 $dtst
	log_must verify_noperm $dtst $LOCAL_DESC_SET $OTHER1

	log_must verify_perm $dtst $LOCAL_DESC_SET $OTHER2
	if [[ $dtst == $ROOT_TESTFS ]]; then
		log_must verify_perm $SUBFS $LOCAL_DESC_SET $OTHER1 $OTHER2
		log_must verify_perm $SUBFS $DESC_SET $STAFF2
	fi
done

log_pass "Verify '-l' only removed the local permissions passed."
