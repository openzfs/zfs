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
#	Verify option '-u', '-g' and '-e' only removed the specified type
#	permissions set.
#
# STRATEGY:
#	1. Allow '-u' '-g' & '-e' to $STAFF1 on ROOT_TESTFS or $ROOT_TESTVOL.
#	2. Unallow '-u' '-g' & '-e' on $ROOT_TESTFS or $ROOT_TESTVOL separately.
#	3. Verify permissions on $ROOT_TESTFS or $ROOT_TESTVOL separately.
#

verify_runnable "both"

log_assert "Verify option '-u', '-g' and '-e' only removed the specified type "\
	"permissions set."
log_onexit restore_root_datasets

for dtst in $DATASETS ; do
	log_must zfs allow -u $STAFF1 $LOCAL_DESC_SET $dtst
	log_must zfs allow -g $STAFF_GROUP $LOCAL_DESC_SET $dtst
	log_must zfs allow -e $LOCAL_DESC_SET $dtst

	log_must verify_perm $dtst $LOCAL_DESC_SET \
		$STAFF1 $STAFF2 $OTHER1 $OTHER2

	log_must zfs unallow -e $dtst
	log_must verify_perm $dtst $LOCAL_DESC_SET $STAFF1 $STAFF2
	log_must verify_noperm $dtst $LOCAL_DESC_SET $OTHER1 $OTHER2

	log_must zfs unallow -g $STAFF_GROUP $dtst
	log_must verify_perm $dtst $LOCAL_DESC_SET $STAFF1
	log_must verify_noperm $dtst $LOCAL_DESC_SET $STAFF2

	log_must zfs unallow -u $STAFF1 $dtst
	log_must verify_noperm $dtst $LOCAL_DESC_SET $STAFF1
done

log_pass "Verify option '-u', '-g' and '-e' passed."
