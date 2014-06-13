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
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013 by Delphix. All rights reserved.
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
	log_must $ZFS allow -u $STAFF1 $LOCAL_DESC_SET $dtst
	log_must $ZFS allow -g $STAFF_GROUP $LOCAL_DESC_SET $dtst
	log_must $ZFS allow -e $LOCAL_DESC_SET $dtst

	log_must verify_perm $dtst $LOCAL_DESC_SET \
		$STAFF1 $STAFF2 $OTHER1 $OTHER2

	log_must $ZFS unallow -e $dtst
	log_must verify_perm $dtst $LOCAL_DESC_SET $STAFF1 $STAFF2
	log_must verify_noperm $dtst $LOCAL_DESC_SET $OTHER1 $OTHER2

	log_must $ZFS unallow -g $STAFF_GROUP $dtst
	log_must verify_perm $dtst $LOCAL_DESC_SET $STAFF1
	log_must verify_noperm $dtst $LOCAL_DESC_SET $STAFF2

	log_must $ZFS unallow -u $STAFF1 $dtst
	log_must verify_noperm $dtst $LOCAL_DESC_SET $STAFF1
done

log_pass "Verify option '-u', '-g' and '-e' passed."
