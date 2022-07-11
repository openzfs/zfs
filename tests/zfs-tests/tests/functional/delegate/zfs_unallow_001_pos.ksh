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
